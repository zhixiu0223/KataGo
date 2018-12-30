#include "../tests/tests.h"
#include "../search/asyncbot.h"
#include "../dataio/sgf.h"
#include <algorithm>
#include <iterator>
using namespace TestCommon;

static string getSearchRandSeed() {
  static int seedCounter = 0;
  return string("testSearchSeed") + Global::intToString(seedCounter++);
}

struct TestSearchOptions {
  int numMovesInARow;
  bool printRootPolicy;
  bool printEndingScoreValueBonus;
  TestSearchOptions()
    :numMovesInARow(1),
     printRootPolicy(false),
     printEndingScoreValueBonus(false)
  {}
};

static void printPolicyValueOwnership(const Board& board, const NNResultBuf& buf) {
  cout << board << endl;
  cout << endl;
  cout << "Win " << Global::strprintf("%.2fc",buf.result->whiteWinProb*100) << endl;
  cout << "Loss " << Global::strprintf("%.2fc",buf.result->whiteLossProb*100) << endl;
  cout << "NoResult " << Global::strprintf("%.2fc",buf.result->whiteNoResultProb*100) << endl;
  cout << "ScoreMean " << Global::strprintf("%.1f",buf.result->whiteScoreMean) << endl;
  cout << "ScoreMeanSq " << Global::strprintf("%.1f",buf.result->whiteScoreMeanSq) << endl;

  cout << "Policy" << endl;
  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      int pos = NNPos::xyToPos(x,y,buf.result->posLen);
      float prob = buf.result->policyProbs[pos];
      if(prob < 0)
        cout << "   - ";
      else
        cout << Global::strprintf("%4d ", (int)round(prob * 1000));
    }
    cout << endl;
  }

  for(int y = 0; y<board.y_size; y++) {
    for(int x = 0; x<board.x_size; x++) {
      int pos = NNPos::xyToPos(x,y,buf.result->posLen);
      float whiteOwn = buf.result->whiteOwnerMap[pos];
      cout << Global::strprintf("%5d ", (int)round(whiteOwn * 1000));
    }
    cout << endl;
  }
  cout << endl;
}

static void runBotOnSgf(AsyncBot* bot, const string& sgfStr, const Rules& rules, int turnNumber, double overrideKomi, TestSearchOptions opts) {
  CompactSgf* sgf = CompactSgf::parse(sgfStr);

  Board board;
  Player nextPla;
  BoardHistory hist;
  sgf->setupBoardAndHist(rules, board, nextPla, hist, turnNumber);
  hist.setKomi(overrideKomi);
  bot->setPosition(nextPla,board,hist);

  Search* search = bot->getSearch();

  for(int i = 0; i<opts.numMovesInARow; i++) {
    Loc move = bot->genMoveSynchronous(nextPla);

    Board::printBoard(cout, board, Board::NULL_LOC, &(hist.moveHistory));

    cout << "Root visits: " << search->numRootVisits() << "\n";
    cout << "NN rows: " << search->nnEvaluator->numRowsProcessed() << endl;
    cout << "NN batches: " << search->nnEvaluator->numBatchesProcessed() << endl;
    cout << "NN avg batch size: " << search->nnEvaluator->averageProcessedBatchSize() << endl;
    cout << "PV: ";
    search->printPV(cout, search->rootNode, 25);
    cout << "\n";
    cout << "Tree:\n";

    PrintTreeOptions options;
    options = options.maxDepth(1);
    search->printTree(cout, search->rootNode, options);

    if(opts.printRootPolicy) {
      search->printRootPolicyMap(cout);
    }
    if(opts.printEndingScoreValueBonus) {
      search->printRootOwnershipMap(cout);
      search->printRootEndingScoreValueBonus(cout);
    }

    bot->makeMove(move, nextPla);
    hist.makeBoardMoveAssumeLegal(board,move,nextPla,NULL);
    nextPla = getOpp(nextPla);
  }

  search->nnEvaluator->clearCache();
  search->nnEvaluator->clearStats();
  bot->clearSearch();

  delete sgf;
}

static NNEvaluator* startNNEval(
  const string& modelFile, Logger& logger, const string& seed, int posLen,
  int defaultSymmetry, bool inputsUseNHWC, bool cudaUseNHWC, bool cudaUseFP16, bool debugSkipNeuralNet, double nnPolicyTemperature
) {
  int modelFileIdx = 0;
  int maxBatchSize = 16;
  bool requireExactPosLen = false;
  //bool inputsUseNHWC = true;
  int nnCacheSizePowerOfTwo = 16;
  int nnMutexPoolSizePowerOfTwo = 12;
  int maxConcurrentEvals = 1024;
  //bool debugSkipNeuralNet = false;
  const string& modelName = modelFile;
  NNEvaluator* nnEval = new NNEvaluator(
    modelName,
    modelFile,
    modelFileIdx,
    maxBatchSize,
    maxConcurrentEvals,
    posLen,
    requireExactPosLen,
    inputsUseNHWC,
    nnCacheSizePowerOfTwo,
    nnMutexPoolSizePowerOfTwo,
    debugSkipNeuralNet,
    nnPolicyTemperature
  );
  (void)inputsUseNHWC;

  int numNNServerThreadsPerModel = 1;
  bool nnRandomize = false;
  string nnRandSeed = "runSearchTestsRandSeed"+seed;
  //int defaultSymmetry = 0;
  vector<int> cudaGpuIdxByServerThread = {0};
  //bool cudaUseFP16 = false;
  //bool cudaUseNHWC = false;

  nnEval->spawnServerThreads(
    numNNServerThreadsPerModel,
    nnRandomize,
    nnRandSeed,
    defaultSymmetry,
    logger,
    cudaGpuIdxByServerThread,
    cudaUseFP16,
    cudaUseNHWC
  );

  return nnEval;
}

static void runBasicPositions(NNEvaluator* nnEval, Logger& logger)
{
  {
    SearchParams params;
    params.maxVisits = 200;
    AsyncBot* bot = new AsyncBot(params, nnEval, &logger, getSearchRandSeed());
    Rules rules = Rules::getTrompTaylorish();
    TestSearchOptions opts;

    {
      cout << "GAME 1 ==========================================================================" << endl;
      cout << "(An ordinary pro game)" << endl;
      cout << endl;

      string sgfStr = "(;SZ[19]FF[3]PW[An Seong-chun]WR[6d]PB[Chen Yaoye]BR[9d]DT[2016-07-02]KM[7.5]RU[Chinese]RE[B+R];B[qd];W[dc];B[pq];W[dp];B[nc];W[po];B[qo];W[qn];B[qp];W[pm];B[nq];W[qi];B[qg];W[oi];B[cn];W[ck];B[fp];W[co];B[dn];W[eo];B[cq];W[dq];B[bo];W[cp];B[bp];W[bq];B[fn];W[bm];B[bn];W[fo];B[go];W[cr];B[en];W[gn];B[ho];W[gm];B[er];W[dr];B[ek];W[di];B[in];W[gk];B[cl];W[dk];B[ej];W[dl];B[el];W[gi];B[fi];W[ch];B[gh];W[hi];B[hh];W[ii];B[eh];W[df];B[ih];W[ji];B[kg];W[fg];B[ff];W[gf];B[eg];W[ef];B[fe];W[ge];B[fd];W[gg];B[fh];W[gd];B[cg];W[dg];B[dh];W[bg];B[bh];W[cf];B[ci];W[qc];B[pc];W[mp];B[on];W[mn];B[om];W[iq];B[pn];W[ol];B[qm];W[pl];B[rn];W[gq];B[kn];W[jo];B[ko];W[jp];B[jn];W[li];B[mo];W[pb];B[rc];W[oc];B[qb];W[od];B[cg];W[pd];B[dd];W[fc];B[ec];W[eb];B[ed];W[cd];B[fb];W[gc];B[db];W[cc];B[ea];W[gb];B[cb];W[bb];B[be];W[ce];B[bf];W[bd];B[ag];W[ca];B[jc];W[qe];B[ep];W[do];B[gp];W[fr];B[qc];W[nb];B[ib];W[je];B[re];W[kd];B[ba];W[aa];B[lc];W[ha];B[ld];W[le];B[me];W[mb];B[ie];W[id];B[kc];W[if];B[lf];W[ke];B[nd];W[of];B[jh];W[qf];B[rf];W[pg];B[mh];W[mq];B[mi];W[mj];B[hl];W[kh];B[jf];W[gl];B[lo];W[np];B[nr];W[kq];B[no];W[he];B[mf];W[rg];B[kk];W[jk];B[kj];W[ki];B[kl];W[lj];B[qk];W[ml];B[pa];W[ob];B[hb];W[ga];B[op];W[mr];B[ms];W[ls];B[ns];W[lq];B[pj];W[oj];B[ng];W[qh];B[eq];W[es];B[rj];W[im];B[jj];W[ik];B[jl];W[il];B[hn];W[hm];B[nm];W[mm];B[nl];W[nk];B[sf];W[ri];B[ql];W[ok];B[qj];W[lb];B[hq];W[hr];B[hp])";


      runBotOnSgf(bot, sgfStr, rules, 20, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 40, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 61, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 82, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 103, 7.5, opts);
      cout << endl << endl;
    }

    {
      cout << "GAME 2 ==========================================================================" << endl;
      cout << "(Another ordinary pro game)" << endl;
      cout << endl;

      string sgfStr = "(;SZ[19]FF[3]PW[Go Seigen]WR[9d]PB[Takagawa Shukaku]BR[8d]DT[1957-09-26]KM[0]RE[W+R];B[qd];W[dc];B[pp];W[cp];B[eq];W[oc];B[ce];W[dh];B[fe];W[gc];B[do];W[co];B[dn];W[cm];B[jq];W[qn];B[pn];W[pm];B[on];W[qq];B[qo];W[or];B[mr];W[mq];B[nr];W[oq];B[lq];W[qm];B[rp];W[rq];B[qg];W[mp];B[lp];W[mo];B[om];W[pk];B[kn];W[mm];B[ok];W[pj];B[mk];W[op];B[dm];W[cl];B[dl];W[dk];B[ek];W[ll];B[cn];W[bn];B[bo];W[bm];B[cq];W[bp];B[oj];W[ph];B[qh];W[oi];B[qi];W[pi];B[mi];W[of];B[ki];W[qc];B[rc];W[qe];B[re];W[pd];B[rd];W[de];B[df];W[cd];B[ee];W[dd];B[fg];W[hd];B[jl];W[dj];B[bf];W[fj];B[hg];W[dp];B[ep];W[jk];B[il];W[fk];B[ie];W[he];B[hf];W[gm];B[ke];W[fo];B[eo];W[in];B[ho];W[hn];B[fn];W[gn];B[go];W[io];B[ip];W[jp];B[hq];W[qf];B[rf];W[qb];B[ik];W[lr];B[id];W[kr];B[jr];W[bq];B[ib];W[hb];B[cr];W[rj];B[rb];W[kk];B[ij];W[ic];B[jc];W[jb];B[hc];W[iq];B[ir];W[ic];B[kq];W[kc];B[hc];W[nj];B[nk];W[ic];B[oe];W[jd];B[pe];W[pf];B[od];W[pc];B[md];W[mc];B[me];W[ld];B[ng];W[ri];B[rh];W[pg];B[fl];W[je];B[kg];W[be];B[cf];W[bh];B[bd];W[bc];B[ae];W[kl];B[rn];W[mj];B[lj];W[ni];B[lk];W[mh];B[li];W[mg];B[mf];W[nh];B[jf];W[qj];B[sh];W[rm];B[km];W[if];B[ig];W[dq];B[dr];W[br];B[ci];W[gi];B[ei];W[ej];B[di];W[gl];B[bi];W[cj];B[sq];W[sr];B[so];W[sp];B[fc];W[fb];B[sq];W[lo];B[rr];W[sp];B[ec];W[eb];B[sq];W[ko];B[jn];W[sp];B[nc];W[nb];B[sq];W[nd];B[jo];W[sp];B[qr];W[pq];B[sq];W[ns];B[ks];W[sp];B[bk];W[bj];B[sq];W[ol];B[nl];W[sp];B[aj];W[ck];B[sq];W[nq];B[ls];W[sp];B[gk];W[qp];B[po];W[ro];B[gj];W[eh];B[rp];W[fi];B[sq];W[pl];B[nm];W[sp];B[ch];W[ro];B[dg];W[sn];B[ne];W[er];B[fr];W[cs];B[es];W[fh];B[bb];W[cb];B[ac];W[ba];B[cc];W[el];B[fm];W[bc])";

      runBotOnSgf(bot, sgfStr, rules, 23, 0, opts);
      runBotOnSgf(bot, sgfStr, rules, 38, 0, opts);
      runBotOnSgf(bot, sgfStr, rules, 65, 0, opts);
      runBotOnSgf(bot, sgfStr, rules, 80, 0, opts);
      runBotOnSgf(bot, sgfStr, rules, 115, 0, opts);
      cout << endl << endl;
    }

    {
      cout << "GAME 3 ==========================================================================" << endl;
      cout << "Extremely close botvbot game" << endl;
      cout << endl;

      string sgfStr = "(;FF[4]GM[1]SZ[19]PB[v49-140-400v-fp16]PW[v49-140-400v-fp16-fpu25]HA[0]KM[7.5]RU[koPOSITIONALscoreAREAsui1]RE[W+0.5];B[qd];W[dp];B[cq];W[dq];B[cp];W[co];B[bo];W[bn];B[cn];W[do];B[bm];W[bp];B[an];W[bq];B[cd];W[qp];B[oq];W[pn];B[nd];W[ec];B[df];W[hc];B[jc];W[cb];B[lq];W[ch];B[cj];W[eh];B[gd];W[gc];B[fd];W[hd];B[gf];W[cl];B[dn];W[el];B[eo];W[fp];B[ej];W[bl];B[bk];W[al];B[cr];W[br];B[fi];W[gl];B[gn];W[gp];B[dk];W[dl];B[fm];W[fl];B[ho];W[iq];B[ip];W[jq];B[jp];W[hp];B[in];W[fh];B[gh];W[gg];B[gi];W[hg];B[fg];W[hf];B[eg];W[il];B[ii];W[kl];B[lo];W[jj];B[ql];W[pq];B[op];W[rm];B[ji];W[ki];B[kh];W[li];B[ij];W[gm];B[dc];W[eb];B[fn];W[jk];B[lk];W[ln];B[ll];W[km];B[mn];W[ko];B[kp];W[mo];B[lp];W[mm];B[nn];W[lm];B[on];W[nk];B[qn];W[qm];B[po];W[rn];B[pm];W[ed];B[cf];W[ni];B[rq];W[rp];B[pr];W[qq];B[qr];W[rr];B[rs];W[sr];B[lc];W[rd];B[rc];W[re];B[pd];W[rb];B[qc];W[qg];B[oj];W[ok];B[pk];W[sc];B[qb];W[bc];B[qh];W[ph];B[qi];W[og];B[kr];W[ff];B[ef];W[fe];B[bd];W[rg];B[oi];W[nh];B[pf];W[pg];B[is];W[hr];B[hs];W[gs];B[gr];W[js];B[fs];W[ir];B[ep];W[eq];B[fq];W[cs];B[er];W[dr];B[fo];W[gq];B[go];W[fr];B[ie];W[he];B[fq];W[hq];B[ib];W[bj];B[bi];W[aj];B[ai];W[ak];B[ci];W[rl];B[ee];W[ge];B[rk];W[ol];B[pl];W[mf];B[nf];W[of];B[ne];W[mg];B[qf];W[rf];B[hb];W[ad];B[jr];W[gs];B[hs];W[es];B[ds];W[fr];B[cc];W[bb];B[fq];W[es];B[ae];W[ac];B[ds];W[fr];B[lh];W[mh];B[fq];W[es];B[qo];W[ro];B[ds];W[fr];B[nj];W[mj];B[fq];W[es];B[bf];W[pi];B[pj];W[ri];B[qj];W[rh];B[gb];W[fb];B[ra];W[sb];B[le];W[me];B[md];W[rj];B[jf];W[sk];B[ks];W[fr];B[lf];W[if];B[id];W[ic];B[je];W[em];B[en];W[ck];B[ga];W[ek];B[dj];W[is];B[kq];W[gs];B[nm];W[nl];B[hk];W[im];B[jn];W[kn];B[gk];W[fk];B[db];W[da];B[fa];W[ea];B[jm];W[jl];B[ih];W[ig];B[jg];W[lg];B[kg];W[oe];B[od];W[dd];B[fj];W[ce];B[be];W[de];B[hh];W[jo];B[io];W[om];B[ap];W[aq];B[ao];W[qk];B[pn];W[am];B[bn];W[pp];B[rk];W[qe];B[pe];W[qk];B[sd];W[se];B[rk];W[qa];B[pa];W[qk];B[hl];W[hm];B[rk];W[jb];B[kb];W[qk];B[qs];W[rk];B[or];W[oh];B[ss];W[sq];B[ng];W[ik];B[hn];W[dm];B[cm];W[sd];B[qa];W[sa];B[kj];W[mk];B[ml];W[mi];B[ja];W[lj];B[dh];W[kk];B[ei];W[fc];B[bh];W[];B[cg];W[];B[no];W[];B[mp];W[];B[])";

      runBotOnSgf(bot, sgfStr, rules, 191, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 197, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 330, 7.5, opts);
      runBotOnSgf(bot, sgfStr, rules, 330, 7.0, opts);

      cout << endl;
      cout << "Jigo and drawUtility===================" << endl;
      cout << "(Game almost over, just a little cleanup)" << endl;
      SearchParams testParams = params;
      testParams.drawEquivalentWinsForWhite = 0.7;
      cout << "testParams.drawEquivalentWinsForWhite = 0.7" << endl;
      cout << endl;

      bot->setParams(testParams);
      cout << "Komi 7.5 (white wins by 0.5)" << endl;
      runBotOnSgf(bot, sgfStr, rules, 330, 7.5, opts);
      cout << endl;

      cout << "Komi 7.0 (draw)" << endl;
      runBotOnSgf(bot, sgfStr, rules, 330, 7.0, opts);
      bot->setParams(params);

      cout << endl;
      cout << "Consecutive searches playouts and visits===================" << endl;
      cout << "Doing three consecutive searches by visits" << endl;
      cout << endl;
      TestSearchOptions opts2 = opts;
      opts2.numMovesInARow = 3;
      runBotOnSgf(bot, sgfStr, rules, 85, 7.5, opts2);
      cout << endl;

      cout << "Doing three consecutive searches by playouts (limit 200)" << endl;
      cout << endl;
      testParams = params;
      testParams.maxPlayouts = 200;
      testParams.maxVisits = 10000;
      bot->setParams(testParams);
      runBotOnSgf(bot, sgfStr, rules, 85, 7.5, opts2);
      bot->setParams(params);
      cout << endl << endl;
    }

    {
      cout << "GAME 4 ==========================================================================" << endl;
      cout << "(A pro game)" << endl;
      cout << endl;

      string sgfStr = "(;SZ[19]FF[3]PW[Gu Li]WR[9d]PB[Ke Jie]BR[9d]DT[2015-07-19]KM[7.5]RU[Chinese]RE[B+R];B[qe];W[dd];B[op];W[dp];B[fc];W[cf];B[jd];W[pc];B[nc];W[nd];B[mc];W[pe];B[pf];W[qd];B[re];W[oe];B[rd];W[rc];B[qb];W[qc];B[qj];W[md];B[ld];W[lc];B[lb];W[kc];B[kb];W[qp];B[qq];W[rq];B[pq];W[ro];B[oc];W[pb];B[le];W[kq];B[fq];W[eq];B[fp];W[dn];B[iq];W[ko];B[io];W[mq];B[pm];W[qn];B[mo];W[oo];B[lp];W[np];B[no];W[oq];B[pp];W[po];B[or];W[lq];B[nq];W[mp];B[mr];W[mm];B[cc];W[dc];B[db];W[eb];B[cb];W[ec];B[be];W[bf];B[fb];W[fa];B[ce];W[de];B[df];W[ae];B[cd];W[fd];B[ad];W[ch];B[ef];W[hc];B[ib];W[gf];B[eh];W[cj];B[ga];W[ea];B[gb];W[hb];B[ha];W[ee];B[ff];W[fe];B[he];W[ge];B[gh];W[ie];B[cl];W[dk];B[cq];W[cp];B[er];W[dr];B[dq];W[ep];B[cr];W[fo];B[go];W[fn];B[fk];W[dl];B[ln];W[jn];B[lm];W[lo];B[ml];W[fr];B[ds];W[hq];B[gr];W[hr];B[ir];W[gp];B[fs];W[ho];B[in];W[im];B[nm];W[jl];B[ii];W[ig];B[of];W[nf];B[mf];W[ng];B[od];W[ne];B[pd];W[qg];B[rg];W[ph];B[qi];W[lh];B[jp];W[kp];B[hm];W[hn];B[qf];W[kj];B[gm];W[gn];B[ik];W[rb];B[jk];W[oj];B[il];W[jm];B[jr];W[lr];B[ms];W[ip];B[kk];W[jo];B[kg];W[ol];B[nk];W[ok];B[om];W[mj];B[sc];W[qk];B[rk];W[ql];B[sb];W[lk];B[kl];W[af];B[rl];W[gc];B[ia];W[id];B[jc];W[rm];B[ab];W[fl];B[gk];W[bq];B[mg];W[ll];B[km];W[nh];B[nl];W[sl];B[rj];W[rh];B[qh];W[pg];B[sf];W[qm];B[br];W[bp];B[di];W[bi];B[ek];W[dj];B[ej])";

      runBotOnSgf(bot, sgfStr, rules, 44, 7.5, opts);

      cout << "With noise===================" << endl;
      cout << "Adding root noise to the search" << endl;
      cout << endl;

      SearchParams testParams = params;
      testParams.rootNoiseEnabled = true;
      testParams.rootFpuReductionMax = 0.0;
      bot->setParams(testParams);
      runBotOnSgf(bot, sgfStr, rules, 44, 7.5, opts);
      bot->setParams(params);
      cout << endl << endl;

      cout << "With root temperature===================" << endl;
      cout << "Adding root policy temperature 1.5 to the search" << endl;
      cout << endl;

      SearchParams testParams2 = params;
      testParams2.rootPolicyTemperature = 1.5;
      bot->setParams(testParams2);
      runBotOnSgf(bot, sgfStr, rules, 44, 7.5, opts);
      bot->setParams(params);
      cout << endl << endl;
    }

    delete bot;
  }
}

static void runOwnershipAndMisc(NNEvaluator* nnEval, NNEvaluator* nnEval11, NNEvaluator* nnEvalPTemp, Logger& logger)
{
  {
    cout << "GAME 5 ==========================================================================" << endl;
    cout << "(A simple opening to test neural net outputs including ownership map)" << endl;

    string sgfStr = "(;FF[4]CA[UTF-8]KM[7.5];B[pp];W[pc];B[cd];W[dq];B[ed];W[pe];B[co];W[cp];B[do];W[fq];B[ck];W[qn];B[qo];W[pn];B[np];W[qj];B[jc];W[lc];B[je];W[lq];B[mq];W[lp];B[ek];W[qq];B[pq];W[ro];B[rp];W[qp];B[po];W[rq];B[rn];W[sp];B[rm];W[ql];B[on];W[om];B[nn];W[nm];B[mn];W[ip];B[mm])";
    CompactSgf* sgf = CompactSgf::parse(sgfStr);

    Board board;
    Player nextPla;
    BoardHistory hist;
    sgf->setupBoardAndHist(Rules::getTrompTaylorish(), board, nextPla, hist, 40);

    double drawEquivalentWinsForWhite = 0.5;
    NNResultBuf buf;
    bool skipCache = true;
    bool includeOwnerMap = true;
    nnEval->evaluate(board,hist,nextPla,drawEquivalentWinsForWhite,buf,NULL,skipCache,includeOwnerMap);

    printPolicyValueOwnership(board,buf);

    nnEval->clearCache();
    nnEval->clearStats();
    cout << endl << endl;

    cout << "With root temperature===================" << endl;
    nnEvalPTemp->evaluate(board,hist,nextPla,drawEquivalentWinsForWhite,buf,NULL,skipCache,includeOwnerMap);

    printPolicyValueOwnership(board,buf);

    nnEvalPTemp->clearCache();
    nnEvalPTemp->clearStats();
    cout << endl << endl;

    delete sgf;
  }

  {
    cout << "GAME 6 ==========================================================================" << endl;
    cout << "(A simple smaller game, also testing invariance under poslen)" << endl;

    string sgfStr = "(;FF[4]CA[UTF-8]SZ[11]KM[7.5];B[ci];W[ic];B[ih];W[hi];B[ii];W[ij];B[jj];W[gj];B[ik];W[di];B[hh];W[ch];B[dc];W[cc];B[cb];W[cd];B[eb];W[dd];B[ed];W[ee];B[fd];W[bb];B[ba];W[ab];B[gb];W[je];B[ib];W[jb];B[jc];W[jd];B[hc];W[id];B[dh];W[cg];B[dj];W[ei];B[bi];W[ia];B[hb];W[fg];B[hj];W[eh];B[ej])";
    CompactSgf* sgf = CompactSgf::parse(sgfStr);

    Board board;
    Player nextPla;
    BoardHistory hist;
    sgf->setupBoardAndHist(Rules::getTrompTaylorish(), board, nextPla, hist, 43);

    double drawEquivalentWinsForWhite = 0.5;
    NNResultBuf buf;
    bool skipCache = true;
    bool includeOwnerMap = true;
    nnEval->evaluate(board,hist,nextPla,drawEquivalentWinsForWhite,buf,NULL,skipCache,includeOwnerMap);
    printPolicyValueOwnership(board,buf);

    cout << "PosLen 11" << endl;
    NNResultBuf buf11;
    nnEval11->evaluate(board,hist,nextPla,drawEquivalentWinsForWhite,buf11,NULL,skipCache,includeOwnerMap);
    testAssert(buf11.result->posLen == 11);
    printPolicyValueOwnership(board,buf11);

    nnEval->clearCache();
    nnEval->clearStats();
    nnEval11->clearCache();
    nnEval11->clearStats();
    delete sgf;
    cout << endl << endl;
  }

  {
    cout << "GAME 7 ==========================================================================" << endl;
    cout << "(Simple extension of game 6 to test root ending bonus points)" << endl;

    SearchParams params;
    params.maxVisits = 500;
    params.fpuReductionMax = 0.0;
    params.rootFpuReductionMax = 0.0;
    AsyncBot* bot = new AsyncBot(params, nnEval, &logger, getSearchRandSeed());
    Rules rules = Rules::getTrompTaylorish();
    TestSearchOptions opts;
    opts.printEndingScoreValueBonus = true;

    string sgfStr = "(;FF[4]CA[UTF-8]SZ[11]KM[7.5];B[ci];W[ic];B[ih];W[hi];B[ii];W[ij];B[jj];W[gj];B[ik];W[di];B[hh];W[ch];B[dc];W[cc];B[cb];W[cd];B[eb];W[dd];B[ed];W[ee];B[fd];W[bb];B[ba];W[ab];B[gb];W[je];B[ib];W[jb];B[jc];W[jd];B[hc];W[id];B[dh];W[cg];B[dj];W[ei];B[bi];W[ia];B[hb];W[fg];B[hj];W[eh];B[ej];W[fj];B[bh];W[bg];B[fe];W[ef];B[jf];W[kc];B[ke];W[ja];B[if];W[fi];B[gg];W[ek];B[ck];W[bj];B[aj];W[bk];B[ah];W[ag];B[cj];W[he];B[hf];W[hd];B[ff];W[kd];B[kf];W[ha];B[gd];W[ga];B[fa];W[gi];B[hk];W[gh];B[ca];W[gk];B[aa];W[bc];B[ge];W[ig];B[fc];W[ka];B[da];W[jg];B[de];W[ce];B[ak];W[ie];B[dk];W[fk];B[hg];W[dg];B[jh];W[ad])";

    cout << "With root ending bonus pts===================" << endl;
    cout << endl;
    SearchParams params2 = params;
    params2.rootEndingBonusPoints = 0.5;
    bot->setParams(params2);
    runBotOnSgf(bot, sgfStr, rules, 88, 7.5, opts);
    cout << endl << endl;

    cout << "With root ending bonus pts one step later===================" << endl;
    cout << endl;
    bot->setParams(params2);
    runBotOnSgf(bot, sgfStr, rules, 89, 7.5, opts);

    cout << "Without root ending bonus pts later later===================" << endl;
    cout << endl;
    bot->setParams(params);
    runBotOnSgf(bot, sgfStr, rules, 96, 7.5, opts);

    cout << "With root ending bonus pts later later===================" << endl;
    cout << endl;
    bot->setParams(params2);
    runBotOnSgf(bot, sgfStr, rules, 96, 7.5, opts);

    delete bot;
  }

  {
    cout << "GAME 8 ==========================================================================" << endl;
    cout << "(Alternate variation of game 7 to test root ending bonus points in territory scoring)" << endl;

    SearchParams params;
    params.maxVisits = 500;
    params.fpuReductionMax = 0.0;
    params.rootFpuReductionMax = 0.0;
    AsyncBot* bot = new AsyncBot(params, nnEval, &logger, getSearchRandSeed());
    Rules rules = Rules::getSimpleTerritory();
    TestSearchOptions opts;
    opts.printEndingScoreValueBonus = true;

    string sgfStr = "(;FF[4]CA[UTF-8]SZ[11]KM[7.5];B[ci];W[ic];B[ih];W[hi];B[ii];W[ij];B[jj];W[gj];B[ik];W[di];B[hh];W[ch];B[dc];W[cc];B[cb];W[cd];B[eb];W[dd];B[ed];W[ee];B[fd];W[bb];B[ba];W[ab];B[gb];W[je];B[ib];W[jb];B[jc];W[jd];B[hc];W[id];B[dh];W[cg];B[dj];W[ei];B[bi];W[ia];B[hb];W[fg];B[hj];W[eh];B[ej];W[fj];B[bh];W[bg];B[fe];W[ef];B[jf];W[kc];B[ke];W[ja];B[if];W[fi];B[gg];W[ek];B[ck];W[bj];B[aj];W[bk];B[ah];W[ag];B[cj];W[he];B[hf];W[hd];B[ff];W[kd];B[kf];W[ha];B[gd];W[ga];B[fa];W[gi];B[hk];W[gh];B[ca];W[gk];B[aa];W[bc];B[ge];W[ig];B[fc];W[ka];B[da];W[jg];B[de];W[ce];B[ak];W[hg];B[gf])";

    cout << "Without root ending bonus pts===================" << endl;
    cout << endl;
    bot->setParams(params);
    runBotOnSgf(bot, sgfStr, rules, 91, 7.5, opts);

    cout << "With root ending bonus pts===================" << endl;
    cout << endl;
    SearchParams params2 = params;
    params2.rootEndingBonusPoints = 0.5;
    bot->setParams(params2);
    runBotOnSgf(bot, sgfStr, rules, 91, 7.5, opts);
    cout << endl << endl;

    delete bot;
  }

  {
    cout << "GAME 9 ==========================================================================" << endl;
    cout << "(A game to visualize root noise)" << endl;
    cout << endl;

    SearchParams params;
    params.maxVisits = 1;
    AsyncBot* bot = new AsyncBot(params, nnEval, &logger, getSearchRandSeed());
    Rules rules = Rules::getSimpleTerritory();
    TestSearchOptions opts;
    opts.printRootPolicy = true;

    string sgfStr = "(;FF[4]CA[UTF-8]SZ[15]KM[7.5];B[lm];W[lc];B[dm];W[dc];B[me];W[md];B[le];W[jc];B[lk];W[ck];B[cl];W[dk];B[fm];W[de];B[dg];W[fk];B[fg];W[hk];B[hm];W[hg];B[ci];W[cf];B[cg];W[mh];B[kh];W[mj];B[lj];W[mk];B[ml];W[nl];B[nm];W[nk];B[hi];W[gh];B[gi];W[fh];B[fi];W[eh];B[ei];W[dh];B[di];W[ch];B[bh];W[eg];B[bg];W[kg];B[jg];W[kf];B[lh];W[mg];B[lg];W[lf];B[mf];W[ke];B[kd];W[je];B[ld];W[jd];B[mc];W[nd];B[kc];W[lb];B[kb];W[mb];B[ne];W[nc];B[ng];W[nh];B[if];W[jh];B[ih];W[ji];B[li];W[ig];B[ij];W[og];B[ef];W[ff];B[ee];W[df];B[fe];W[gf];B[ec];W[db];B[dd];W[cd];B[ed];W[bf];B[eb];W[he];B[gd];W[hc];B[gc];W[hd];B[gb];W[hb];B[fc];W[fa];B[ea];W[bc];B[ga];W[jj];B[ik];W[jk];B[il];W[jl];B[jm];W[gg];B[ge];W[kl];B[km];W[bl];B[ll];W[cn])";

    runBotOnSgf(bot, sgfStr, rules, 114, 6.5, opts);

    cout << "With noise===================" << endl;
    cout << "Adding root noise to the search" << endl;
    cout << endl;

    SearchParams testParams = params;
    testParams.rootNoiseEnabled = true;
    bot->setParams(testParams);
    runBotOnSgf(bot, sgfStr, rules, 114, 6.5, opts);
    bot->setParams(params);
    cout << endl << endl;

    delete bot;
  }

}


void Tests::runSearchTests(const string& modelFile, bool inputsNHWC, bool cudaNHWC, int symmetry, bool useFP16) {
  cout << "Running search tests" << endl;
  string tensorflowGpuVisibleDeviceList = "";
  double tensorflowPerProcessGpuMemoryFraction = 0.3;
  NeuralNet::globalInitialize(tensorflowGpuVisibleDeviceList,tensorflowPerProcessGpuMemoryFraction);

  Logger logger;
  logger.setLogToStdout(true);
  logger.setLogTime(false);

  NNEvaluator* nnEval = startNNEval(modelFile,logger,"",NNPos::MAX_BOARD_LEN,symmetry,inputsNHWC,cudaNHWC,useFP16,false,1.0);
  runBasicPositions(nnEval, logger);
  delete nnEval;

  NeuralNet::globalCleanup();
}

void Tests::runSearchTestsV3(const string& modelFile, bool inputsNHWC, bool cudaNHWC, int symmetry, bool useFP16) {
  cout << "Running search tests specifically for v3 or later nets" << endl;
  string tensorflowGpuVisibleDeviceList = "";
  double tensorflowPerProcessGpuMemoryFraction = 0.3;
  NeuralNet::globalInitialize(tensorflowGpuVisibleDeviceList,tensorflowPerProcessGpuMemoryFraction);

  Logger logger;
  logger.setLogToStdout(true);
  logger.setLogTime(false);

  NNEvaluator* nnEval = startNNEval(modelFile,logger,"",NNPos::MAX_BOARD_LEN,symmetry,inputsNHWC,cudaNHWC,useFP16,false,1.0);
  NNEvaluator* nnEval11 = startNNEval(modelFile,logger,"",11,symmetry,inputsNHWC,cudaNHWC,useFP16,false,1.0);
  NNEvaluator* nnEvalPTemp = startNNEval(modelFile,logger,"",NNPos::MAX_BOARD_LEN,symmetry,inputsNHWC,cudaNHWC,useFP16,false,1.5);
  runOwnershipAndMisc(nnEval,nnEval11,nnEvalPTemp,logger);
  delete nnEval;

  NeuralNet::globalCleanup();
}



void Tests::runNNLessSearchTests() {
  cout << "Running neuralnetless search tests" << endl;
  string tensorflowGpuVisibleDeviceList = "";
  double tensorflowPerProcessGpuMemoryFraction = 0.3;
  NeuralNet::globalInitialize(tensorflowGpuVisibleDeviceList,tensorflowPerProcessGpuMemoryFraction);

  //Placeholder, doesn't actually do anything since we have debugSkipNeuralNet = true
  string modelFile = "/dev/null";

  Logger logger;
  logger.setLogToStdout(false);
  logger.setLogTime(false);
  logger.addOStream(cout);

  {
    cout << "===================================================================" << endl;
    cout << "Basic search with debugSkipNeuralNet and chosen move randomization" << endl;
    cout << "===================================================================" << endl;

    NNEvaluator* nnEval = startNNEval(modelFile,logger,"",NNPos::MAX_BOARD_LEN,0,true,false,false,true,1.0);
    SearchParams params;
    params.maxVisits = 100;
    Search* search = new Search(params, nnEval, "autoSearchRandSeed");
    Rules rules = Rules::getTrompTaylorish();
    TestSearchOptions opts;

    Board board = Board::parseBoard(9,9,R"%%(
.........
.........
..x..o...
.........
..x...o..
...o.....
..o.x.x..
.........
.........
)%%");
    Player nextPla = P_BLACK;
    BoardHistory hist(board,nextPla,rules,0);

    search->setPosition(nextPla,board,hist);
    search->runWholeSearch(nextPla,logger,NULL);

    PrintTreeOptions options;
    options = options.maxDepth(1);
    search->printTree(cout, search->rootNode, options);

    auto sampleChosenMoves = [&]() {
      std::map<Loc,int> moveLocsAndCounts;
      for(int i = 0; i<10000; i++) {
        Loc loc = search->getChosenMoveLoc();
        moveLocsAndCounts[loc] += 1;
      }
      vector<pair<Loc,int>> moveLocsAndCountsSorted;
      std::copy(moveLocsAndCounts.begin(),moveLocsAndCounts.end(),std::back_inserter(moveLocsAndCountsSorted));
      std::sort(moveLocsAndCountsSorted.begin(), moveLocsAndCountsSorted.end(), [](pair<Loc,int> a, pair<Loc,int> b) { return a.second > b.second; });

      for(int i = 0; i<moveLocsAndCountsSorted.size(); i++) {
        cout << Location::toString(moveLocsAndCountsSorted[i].first,board) << " " << moveLocsAndCountsSorted[i].second << endl;
      }
    };

    cout << "Chosen moves at temperature 0" << endl;
    sampleChosenMoves();

    {
      cout << "Chosen moves at temperature 1 but early temperature 0, when it's perfectly early" << endl;
      search->searchParams.chosenMoveTemperature = 1.0;
      search->searchParams.chosenMoveTemperatureEarly = 0.0;
      sampleChosenMoves();
    }

    {
      cout << "Chosen moves at temperature 1" << endl;
      search->searchParams.chosenMoveTemperature = 1.0;
      search->searchParams.chosenMoveTemperatureEarly = 1.0;
      sampleChosenMoves();
    }

    {
      cout << "Chosen moves at some intermediate temperature" << endl;
      //Ugly hack to artifically fill history. Breaks all sorts of invariants, but should work to
      //make the search htink there's some history to choose an intermediate temperature
      for(int i = 0; i<16; i++)
        search->rootHistory.moveHistory.push_back(Move(Board::NULL_LOC,P_BLACK));

      search->searchParams.chosenMoveTemperature = 1.0;
      search->searchParams.chosenMoveTemperatureEarly = 0.0;
      search->searchParams.chosenMoveTemperatureHalflife = 16.0 * 19.0/9.0;
      sampleChosenMoves();
    }

    delete search;
    delete nnEval;
    cout << endl;
  }

  {
    cout << "===================================================================" << endl;
    cout << "Testing preservation of search tree across moves" << endl;
    cout << "===================================================================" << endl;

    NNEvaluator* nnEval = startNNEval(modelFile,logger,"",NNPos::MAX_BOARD_LEN,0,true,false,false,true,1.0);
    SearchParams params;
    params.maxVisits = 50;
    Search* search = new Search(params, nnEval, "autoSearchRandSeed");
    Rules rules = Rules::getTrompTaylorish();
    TestSearchOptions opts;

    Board board = Board::parseBoard(7,7,R"%%(
..xx...
xxxxxxx
.xx..xx
.xxoooo
xxxo...
ooooooo
...o...
)%%");
    Player nextPla = P_BLACK;
    BoardHistory hist(board,nextPla,rules,0);

    {
      //--------------------------------------
      cout << "First perform a basic search." << endl;

      search->setPosition(nextPla,board,hist);
      search->runWholeSearch(nextPla,logger,NULL);

      //In theory nothing requires this, but it would be kind of crazy if this were false
      testAssert(search->rootNode->numChildren > 1);
      Loc locToDescend = search->rootNode->children[1]->prevMoveLoc;

      PrintTreeOptions options;
      options = options.maxDepth(1);
      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);
      search->printTree(cout, search->rootNode, options.onlyBranch(board,Location::toString(locToDescend,board)));

      cout << endl;

      //--------------------------------------
      cout << "Next, make a move, and with no search, print the tree." << endl;

      search->makeMove(locToDescend,nextPla);
      nextPla = getOpp(nextPla);

      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);
      cout << endl;

      //--------------------------------------
      cout << "Then continue the search to complete 50 visits." << endl;

      search->runWholeSearch(nextPla,logger,NULL);
      search->printTree(cout, search->rootNode, options);
      cout << endl;
    }

    delete search;
    delete nnEval;

    cout << endl;
  }

  {
    cout << "===================================================================" << endl;
    cout << "Testing pruning of search tree across moves due to root restrictions" << endl;
    cout << "===================================================================" << endl;

    Board board = Board::parseBoard(7,7,R"%%(
..xx...
xx.xxxx
xxxx.xx
.xxoooo
xxxo..x
ooooooo
o..oo.x
)%%");
    Player nextPla = P_BLACK;
    Rules rules = Rules::getTrompTaylorish();
    BoardHistory hist(board,nextPla,rules,0);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("C6",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("pass",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("G7",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("pass",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("F3",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("pass",board),nextPla,NULL);
    nextPla = getOpp(nextPla);

    auto hasSuicideRootMoves = [](const Search* search) {
      for(int i = 0; i<search->rootNode->numChildren; i++) {
        if(search->rootBoard.isSuicide(search->rootNode->children[i]->prevMoveLoc,search->rootPla))
          return true;
      }
      return false;
    };
    auto hasPassAliveRootMoves = [](const Search* search) {
      for(int i = 0; i<search->rootNode->numChildren; i++) {
        if(search->rootSafeArea[search->rootNode->children[i]->prevMoveLoc] != C_EMPTY)
          return true;
      }
      return false;
    };


    {
      cout << "First with no pruning" << endl;
      NNEvaluator* nnEval = startNNEval(modelFile,logger,"seed1",NNPos::MAX_BOARD_LEN,0,true,false,false,true,1.0);
      SearchParams params;
      params.maxVisits = 400;
      Search* search = new Search(params, nnEval, "autoSearchRandSeed3");
      TestSearchOptions opts;

      search->setPosition(nextPla,board,hist);
      search->runWholeSearch(nextPla,logger,NULL);
      PrintTreeOptions options;
      options = options.maxDepth(1);
      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);

      testAssert(hasSuicideRootMoves(search));

      delete search;
      delete nnEval;

      cout << endl;
    }

    {
      cout << "Next, with rootPruneUselessSuicides" << endl;
      NNEvaluator* nnEval = startNNEval(modelFile,logger,"seed1",NNPos::MAX_BOARD_LEN,0,true,false,false,true,1.0);
      SearchParams params;
      params.maxVisits = 400;
      params.rootPruneUselessSuicides = true;
      Search* search = new Search(params, nnEval, "autoSearchRandSeed3");
      TestSearchOptions opts;

      search->setPosition(nextPla,board,hist);
      search->runWholeSearch(nextPla,logger,NULL);
      PrintTreeOptions options;
      options = options.maxDepth(1);
      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);

      testAssert(!hasSuicideRootMoves(search));

      delete search;
      delete nnEval;

      cout << endl;
    }

    cout << "Progress the game, having black fill space while white passes..." << endl;
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("A7",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("pass",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("E7",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("pass",board),nextPla,NULL);
    nextPla = getOpp(nextPla);
    hist.makeBoardMoveAssumeLegal(board,Location::ofString("F7",board),nextPla,NULL);
    nextPla = getOpp(nextPla);

    {
      cout << "Searching on the opponent, the move before" << endl;
      NNEvaluator* nnEval = startNNEval(modelFile,logger,"seed1",NNPos::MAX_BOARD_LEN,0,true,false,false,true,1.0);
      SearchParams params;
      params.maxVisits = 400;
      params.rootPruneUselessSuicides = true;
      Search* search = new Search(params, nnEval, "autoSearchRandSeed3");
      TestSearchOptions opts;

      search->setPosition(nextPla,board,hist);
      search->runWholeSearch(nextPla,logger,NULL);
      PrintTreeOptions options;
      options = options.maxDepth(1);
      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);
      search->printTree(cout, search->rootNode, options.onlyBranch(board,"pass"));

      cout << endl;
      
      cout << "Now play forward the pass. The tree should still have useless suicides in it" << endl;
      search->makeMove(Board::PASS_LOC,nextPla);
      testAssert(hasSuicideRootMoves(search));
      testAssert(hasPassAliveRootMoves(search));

      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);

      cout << endl;
      
      cout << "But the moment we begin a search, it should no longer." << endl;
      search->beginSearch(logger);
      testAssert(!hasSuicideRootMoves(search));
      testAssert(!hasPassAliveRootMoves(search));

      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);

      cout << endl;
      
      cout << "Continue searching a bit more" << endl;
      search->runWholeSearch(getOpp(nextPla),logger,NULL);

      cout << search->rootBoard << endl;
      search->printTree(cout, search->rootNode, options);

      delete search;
      delete nnEval;
      cout << endl;
    }


  }

  NeuralNet::globalCleanup();
}
