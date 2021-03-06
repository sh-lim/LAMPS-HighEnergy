//#define PULLIN
//#define SINGlETRACK_DEBUGGER
//#define CONTINUITY_DEBUGGER
//#define VERTEX_POINT_ADDED

#include "KBRun.hh"
#include "LHTrackFinder.hh"

#include <iostream>

ClassImp(LHTrackFinder)

bool LHTrackFinder::Init()
{
  fPadPlane = (LHPadPlane *) fTpc -> GetPadPlane();

  fCandHits = new KBTpcHits;
  fGoodHits = new KBTpcHits;
  fBadHits = new KBTpcHits;

  fDefaultScale = fPar -> GetParDouble("LHTF_defaultScale");
  fTrackWCutLL = fPar -> GetParDouble("LHTF_trackWCutLL");
  fTrackWCutHL = fPar -> GetParDouble("LHTF_trackWCutHL");
  fTrackHCutLL = fPar -> GetParDouble("LHTF_trackHCutLL");
  fTrackHCutHL = fPar -> GetParDouble("LHTF_trackHCutHL");

  fReferenceAxis = fPar -> GetParAxis("tpcBFieldAxis");

  fVertexPoint = new KBTpcHit();
  fVertexPoint -> Set(0,0,0,1);

  return true;
}

void LHTrackFinder::FindTrack(TClonesArray *in, TClonesArray *out)
{
#ifdef PRINT_PROCESS_SUMMARY
  fCountNew = 0;
  fCountInit = 0;
  fCountConti = 0;
  fCountExtrap = 0;
  fCountConfirm = 0;
#endif
#ifdef CHECK_INITIAL_HITS
  fCheckHitIndex = CHECK_INITIAL_HITS;
#endif
  fTrackArray = out;

  fPadPlane -> ResetHitMap();
  fPadPlane -> SetHitArray(in);

#ifdef CHECK_INITIAL_HITS
  fCheckHitIndex = CHECK_INITIAL_HITS;
#endif

  fCandHits -> clear();
  fGoodHits -> clear();
  fBadHits -> clear();

  while(1)
  {
    KBHelixTrack *track = NewTrack();
    if (track == nullptr)
      break;

    bool survive = false;

    if (InitTrack(track))
    {
      if (TrackContinuum(track))
        if (TrackExtrapolation(track)) {
          TrackConfirmation(track);
          survive = true;
        }
    }

    Int_t numBadHits = fBadHits -> size();
    for (Int_t iHit = 0; iHit < numBadHits; ++iHit)
      fPadPlane -> AddHit(fBadHits -> at(iHit));
    fBadHits -> clear();

    //if (track -> GetNumHits() < 10) survive = false;

    if (survive) {
      auto trackHits = track -> GetHitArray();
      Int_t trackID = track -> GetTrackID();
      Int_t numTrackHits = trackHits -> size();
      for (Int_t iTrackHit = 0; iTrackHit < numTrackHits; ++iTrackHit) {
        auto trackHit = (KBTpcHit *) trackHits -> at(iTrackHit);
        trackHit -> AddTrackCand(trackID);
        fPadPlane -> AddHit(trackHit);
      }
    }
    else {
      auto trackHits = track -> GetHitArray();
      Int_t numTrackHits = trackHits -> size();
      for (Int_t iTrackHit = 0; iTrackHit < numTrackHits; ++iTrackHit) {
        auto trackHit = (KBTpcHit *) trackHits -> at(iTrackHit);
        trackHit -> AddTrackCand(-1);
        fPadPlane -> AddHit(trackHit);
      }
      fTrackArray -> Remove(track);
    }
  }

  fTrackArray -> Compress();

  Int_t numTracks = fTrackArray -> GetEntriesFast();
  for (Int_t iTrack = 0; iTrack < numTracks; ++iTrack) {
    KBHelixTrack *track = (KBHelixTrack *) fTrackArray -> At(iTrack);
    track -> FinalizeHits();
  }
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << " newtrk:" << fCountNew
          << " init:" << fCountInit
          << " conti:" << fCountConti
          << " extrap:" << fCountExtrap
          << " confirm:" << fCountConfirm << endl;
#endif
}

KBHelixTrack *LHTrackFinder::NewTrack()
{
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << fCountNew << endl;
  fCountNew++;
#endif
  KBTpcHit *hit = fPadPlane -> PullOutNextFreeHit();
#ifdef CHECK_INITIAL_HITS
  if (fCheckHitIndex>0) {
    hit -> Print();
    fPadPlane -> GetPad(hit -> GetPadID()) -> Print();
    fCheckHitIndex--;
  }
#endif
  if (hit == nullptr)
    return nullptr;

  Int_t idx = fTrackArray -> GetEntries();
  KBHelixTrack *track = new ((*fTrackArray)[idx]) KBHelixTrack(idx);
  track -> SetReferenceAxis(fReferenceAxis);
  track -> AddHit(hit);

  fGoodHits -> push_back(hit);

  return track;
}

bool LHTrackFinder::InitTrack(KBHelixTrack *track)
{
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << fCountInit << endl;
  fCountInit++;
#endif
#ifdef PULLIN
  fPadPlane -> PullOutNeighborHitsIn(fGoodHits, fCandHits);
#else
  fPadPlane -> PullOutNeighborHits(fGoodHits, fCandHits);
#endif
  fGoodHits -> clear();

  Int_t numCandHits = fCandHits -> size();;
  while (numCandHits != 0) {
    sort(fCandHits->begin(), fCandHits->end(), KBHitSortByDistanceTo(track->GetMean()));

    for (Int_t iHit = 0; iHit < numCandHits; iHit++) {
      KBTpcHit *candHit = fCandHits -> back();
      fCandHits -> pop_back();

      Double_t quality = CorrelateSimple(track, candHit);

      if (quality > 0) {
        fGoodHits -> push_back(candHit);
        track -> AddHit(candHit);

        if (track -> GetNumHits() > 15) {
          UInt_t numCandHits2 = fCandHits -> size();
          for (UInt_t iCand = 0; iCand < numCandHits2; ++iCand)
            fPadPlane -> AddHit(fCandHits -> at(iCand));
          fCandHits -> clear();
          break;
        }

        if (track -> GetNumHits() > 6) {
          track -> Fit();
          if (!(track -> GetNumHits() < 10 && track -> GetHelixRadius() < 30) && (track -> TrackLength() > 2.5 * track -> GetRMSR())) {
#ifdef VERTEX_POINT_ADDED
            track -> AddHit(fVertexPoint);
#endif
            return true;
          }
        }

        track -> FitPlane();
      }
      else
        fBadHits -> push_back(candHit);
    }

#ifdef PULLIN
    fPadPlane -> PullOutNeighborHitsIn(fGoodHits, fCandHits);
#else
    fPadPlane -> PullOutNeighborHits(fGoodHits, fCandHits);
#endif
    fGoodHits -> clear();

    numCandHits = fCandHits -> size();
  }

  for (UInt_t iBad = 0; iBad < fBadHits -> size(); ++iBad)
    fPadPlane -> AddHit(fBadHits -> at(iBad));
  fBadHits -> clear();

  return false;
}

bool LHTrackFinder::TrackContinuum(KBHelixTrack *track)
{
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << fCountConti << endl;
  fCountConti++;
#endif
#ifdef SINGlETRACK_DEBUGGER
  kb_warning << "Continuum; Building Track:" << track -> GetTrackID() << endl;
#endif

#ifdef PULLIN
  fPadPlane -> PullOutNeighborHitsIn(fGoodHits, fCandHits);
#else
  fPadPlane -> PullOutNeighborHits(fGoodHits, fCandHits);
#endif
  fGoodHits -> clear();

  Int_t numCandHits = fCandHits -> size();

  while (numCandHits != 0)
  {
    sort(fCandHits -> begin(), fCandHits -> end(), KBHitSortCharge());

#ifdef SINGlETRACK_DEBUGGER
  kb_warning << "Number of candidates: " << numCandHits << endl;
#endif
    for (Int_t iHit = 0; iHit < numCandHits; iHit++) {
      KBTpcHit *candHit = fCandHits -> back();
      fCandHits -> pop_back();

      Double_t quality = 0; 
      if (CheckHitOwner(candHit) == -2)
        quality = Correlate(track, candHit);

      if (quality > 0) {
        fGoodHits -> push_back(candHit);
        track -> AddHit(candHit);
        track -> Fit();
#ifdef SINGlETRACK_DEBUGGER
        kb_warning << "Hit:" << iHit << " Quality = " << quality << " is good. # of Hits = " << track -> GetNumHits() << endl;
#endif
      } else {
        fBadHits -> push_back(candHit);
#ifdef SINGlETRACK_DEBUGGER
        kb_error << "Hit:" << iHit << " Quality = " << quality << " is bad." << endl;
#endif
      }
    }

#ifdef PULLIN
    fPadPlane -> PullOutNeighborHitsIn(fGoodHits, fCandHits);
#else
    fPadPlane -> PullOutNeighborHits(fGoodHits, fCandHits);
#endif
    fGoodHits -> clear();

    numCandHits = fCandHits -> size();
  }

  return TrackQualityCheck(track);
}

bool LHTrackFinder::TrackExtrapolation(KBHelixTrack *track)
{
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << fCountExtrap << endl;
  fCountExtrap++;
#endif
  for (UInt_t iBad = 0; iBad < fBadHits -> size(); ++iBad)
    fPadPlane -> AddHit(fBadHits -> at(iBad));
  fBadHits -> clear();

  Int_t count = 0;
  bool buildHead = true;
  Double_t extrapolationLength = 0;
  while (AutoBuildByExtrapolation(track, buildHead, extrapolationLength)) {
    if (++count > 200)
      break;
  }

  count = 0;
  buildHead = !buildHead;
  extrapolationLength = 0;
  while (AutoBuildByExtrapolation(track, buildHead, extrapolationLength)) {
    if (++count > 200)
      break;
  }

  for (UInt_t iBad = 0; iBad < fBadHits -> size(); ++iBad)
    fPadPlane -> AddHit(fBadHits -> at(iBad));
  fBadHits -> clear();

  return TrackQualityCheck(track);
}

bool LHTrackFinder::TrackConfirmation(KBHelixTrack *track)
{
#ifdef VERTEX_POINT_ADDED
  track -> RemoveHit(fVertexPoint);
  track -> Fit();
#endif
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << fCountConfirm << endl;
  fCountConfirm++;
#endif
  bool tailToHead = false;
  //if (track -> PositionAtTail().Z() > track -> PositionAtHead().Z())
  KBVector3 pTail(track -> PositionAtTail(), fReferenceAxis);
  KBVector3 pHead(track -> PositionAtHead(), fReferenceAxis);

  if (pHead.K() > pHead.K())
    tailToHead = true;

  for (UInt_t iBad = 0; iBad < fBadHits -> size(); ++iBad)
    fPadPlane -> AddHit(fBadHits -> at(iBad));
  fBadHits -> clear();
  ConfirmHits(track, tailToHead);

  tailToHead = !tailToHead; 

  for (UInt_t iBad = 0; iBad < fBadHits -> size(); ++iBad)
    fPadPlane -> AddHit(fBadHits -> at(iBad));
  fBadHits -> clear();
  ConfirmHits(track, tailToHead);

  for (UInt_t iBad = 0; iBad < fBadHits -> size(); ++iBad)
    fPadPlane -> AddHit(fBadHits -> at(iBad));
  fBadHits -> clear();

  return true;
}

Int_t LHTrackFinder::CheckHitOwner(KBTpcHit *hit)
{
  vector<Int_t> *candTracks = hit -> GetTrackCandArray();
  if (candTracks -> size() == 0)
    return -2;

  Int_t trackID = -1;
  for (UInt_t iCand = 0; iCand < candTracks -> size(); ++iCand) {
    Int_t candTrackID = candTracks -> at(iCand);
    if (candTrackID != -1) {
      trackID = candTrackID;
    }
  }

  return trackID;
}

Double_t LHTrackFinder::Correlate(KBHelixTrack *track, KBTpcHit *hit, Double_t rScale)
{
  Double_t scale = rScale * fDefaultScale;
  Double_t trackLength = track -> TrackLength();
  if (trackLength < 500.)
    scale = scale + (500. - trackLength)/500.;

  /*
  auto direction = track->Momentum().Unit();
  Double_t dot = abs(direction.Dot(KBVector3(fReferenceAxis,0,0,1).GetXYZ()));
  auto a = 1.;
  auto b = 1.;
  if (dot > .5) {
     a = (2*(dot-.5)+1); //
     b = (2*(dot-.5)+1); //
  }
  auto trackHCutLL = a*fTrackHCutLL;
  auto trackHCutHL = a*fTrackHCutHL;
  auto trackWCutLL = b*fTrackWCutLL;
  auto trackWCutHL = b*fTrackWCutHL;
  */
  auto trackHCutLL = fTrackHCutLL;
  auto trackHCutHL = fTrackHCutHL;
  auto trackWCutLL = fTrackWCutLL;
  auto trackWCutHL = fTrackWCutHL;

  Double_t rmsWCut = track -> GetRMSR();
  if (rmsWCut < trackWCutLL) rmsWCut = trackWCutLL;
  if (rmsWCut > trackWCutHL) rmsWCut = trackWCutHL;
  rmsWCut = scale * rmsWCut;

  Double_t rmsHCut = track -> GetRMST();
  if (rmsHCut < trackHCutLL) rmsHCut = trackHCutLL;
  if (rmsHCut > trackHCutHL) rmsHCut = trackHCutHL;
  rmsHCut = scale * rmsHCut;

  TVector3 qHead = track -> Map(track -> PositionAtHead());
  TVector3 qTail = track -> Map(track -> PositionAtTail());
  TVector3 q = track -> Map(hit -> GetPosition());

  if (qHead.Z() > qTail.Z()) {
    if (LengthAlphaCut(track, q.Z() - qHead.Z())) return 0;
    if (LengthAlphaCut(track, qTail.Z() - q.Z())) return 0;
  } else {
    if (LengthAlphaCut(track, q.Z() - qTail.Z())) return 0;
    if (LengthAlphaCut(track, qHead.Z() - q.Z())) return 0;
  }

  Double_t dr = abs(q.X());
  Double_t quality = 0;
  if (dr < rmsWCut && abs(q.Y()) < rmsHCut)
    quality = sqrt((dr-rmsWCut)*(dr-rmsWCut)) / rmsWCut;

  return quality;
}

bool LHTrackFinder::LengthAlphaCut(KBHelixTrack *track, Double_t dLength)
{
  if (dLength > 0) {
    if (dLength > .5*track -> TrackLength()) {
      if (abs(track -> AlphaAtTravelLength(dLength)) > .5*TMath::Pi()) {
        return true;
      }
    }
  }
  return false;
}

Double_t LHTrackFinder::CorrelateSimple(KBHelixTrack *track, KBTpcHit *hit)
{
  if (hit -> GetNumTrackCands() != 0)
    return 0;

  Double_t quality = 0;

  auto row = hit -> GetRow();
  auto layer = hit -> GetLayer();

  KBVector3 pos0(hit -> GetPosition(), fReferenceAxis);

  auto trackHits = track -> GetHitArray();
  bool kcut = false;
  Int_t numTrackHits = trackHits -> size();
  for (Int_t iTrackHit = 0; iTrackHit < numTrackHits; ++iTrackHit) {
    auto trackHit = (KBTpcHit *) trackHits -> at(iTrackHit);
    if (row == trackHit -> GetRow() && layer == trackHit -> GetLayer())
      return 0;
    KBVector3 pos1(trackHit -> GetPosition(), fReferenceAxis);
    auto di = pos0.I() - pos1.I();
    auto dj = pos0.J() - pos1.J();
    auto padDisplacement = sqrt(di*di + dj*dj);
    Double_t kcutv = 1.2 * padDisplacement*abs(pos1.K())/sqrt(pos1.I()*pos1.I()+pos1.J()*pos1.J());

    if (kcutv < 4) kcutv = 4;
    if (abs(pos0.K()-pos1.K()) < kcutv) kcut = true;
  }
  if (kcut == false) {
    return 0;
  }

  ////////////////////////////////////////////////////////////////
  if (track -> IsBad()) {
    quality = 1;
  }
  ////////////////////////////////////////////////////////////////
  else if (track -> IsLine()) {
    KBVector3 perp = track -> PerpLine(hit -> GetPosition());

    Double_t rmsCut = track -> GetRMST();
    if (rmsCut < fTrackHCutLL) rmsCut = fTrackHCutLL;
    if (rmsCut > fTrackHCutHL) rmsCut = fTrackHCutHL;
    rmsCut = 3 * rmsCut;

    if (perp.K() > rmsCut) {
      quality = 0;
    }
    else {
      perp.SetK(0);
      if (perp.Mag() < 15)
      //if (perp.Mag() < 10*pos1.K()/sqrt(pos1.Mag()))
      {
        quality = 1;
      }
    }
  }
  ////////////////////////////////////////////////////////////////
  else if (track -> IsPlane()) {
    Double_t dist = (track -> PerpPlane(hit -> GetPosition())).Mag();

    Double_t rmsCut = track -> GetRMST();
    if (rmsCut < fTrackHCutLL) rmsCut = fTrackHCutLL;
    if (rmsCut > fTrackHCutHL) rmsCut = fTrackHCutHL;
    rmsCut = 3 * rmsCut;

    if (dist < rmsCut) {
      quality = 1;
    }
  }
  ////////////////////////////////////////////////////////////////

  return quality;
}

bool LHTrackFinder::ConfirmHits(KBHelixTrack *track, bool &tailToHead)
{
  track -> SortHits(!tailToHead);
  auto trackHits = track -> GetHitArray();
  Int_t numHits = trackHits -> size();

  TVector3 q, m;
  //Double_t lPre = track -> ExtrapolateByMap(trackHits->at(numHits-1)->GetPosition(), q, m);

  Double_t extrapolationLength = 10.;
  for (Int_t iHit = 1; iHit < numHits; iHit++) 
  {
    auto trackHit = (KBTpcHit *) trackHits -> at(numHits-iHit-1);
    //Double_t lCur = track -> ExtrapolateByMap(trackHit->GetPosition(), q, m);

    Double_t quality = Correlate(track, trackHit);

    if (quality <= 0) {
      track -> RemoveHit(trackHit);
      trackHit -> RemoveTrackCand(trackHit -> GetTrackID());
      Int_t helicity = track -> Helicity();
      track -> Fit();
      if (helicity != track -> Helicity())
        tailToHead = !tailToHead;

      continue;
    }

    /*
    Double_t dLength = abs(lCur - lPre);
    extrapolationLength = 10;
    while(dLength > 0 && AutoBuildByInterpolation(track, tailToHead, extrapolationLength, 1)) { dLength -= 10; }
    */
  }

  extrapolationLength = 0;
  while (AutoBuildByExtrapolation(track, tailToHead, extrapolationLength)) {}

  return true;
}

bool LHTrackFinder::AutoBuildByExtrapolation(KBHelixTrack *track, bool &buildHead, Double_t &extrapolationLength)
{
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << "in" << endl;
#endif
  TVector3 p;
  if (buildHead) p = track -> ExtrapolateHead(extrapolationLength);
  else           p = track -> ExtrapolateTail(extrapolationLength);

  return AutoBuildAtPosition(track, p, buildHead, extrapolationLength);
}

bool LHTrackFinder::AutoBuildByInterpolation(KBHelixTrack *track, bool &tailToHead, Double_t &extrapolationLength, Double_t rScale)
{
  TVector3 p;
  if (tailToHead) p = track -> InterpolateByLength(extrapolationLength);
  else            p = track -> InterpolateByLength(track -> TrackLength() - extrapolationLength);

  return AutoBuildAtPosition(track, p, tailToHead, extrapolationLength, rScale);
}

bool LHTrackFinder::AutoBuildAtPosition(KBHelixTrack *track, TVector3 p, bool &tailToHead, Double_t &extrapolationLength, Double_t rScale)
{
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << "in" << endl;
#endif
  //if (fPadPlane -> IsInBoundary(p.X(), p.Z()) == false)
  KBVector3 p2(p,fReferenceAxis);
  if (fPadPlane -> IsInBoundary(p2.I(), p2.J()) == false)
    return false;

  Int_t helicity = track -> Helicity();

  Double_t rms = 3*track -> GetRMSR();
  if (rms < 25) 
    rms = 25;

  Int_t range = Int_t(rms/8);
  TVector2 q(p2.I(), p2.J());
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << "a" << endl;
#endif
  fPadPlane -> PullOutNeighborHits(q, range, fCandHits);
#ifdef PRINT_PROCESS_SUMMARY
  kb_info << "b" << endl;
#endif

  Int_t numCandHits = fCandHits -> size();
  Bool_t foundHit = false;

  if (numCandHits != 0) 
  {
    sort(fCandHits -> begin(), fCandHits -> end(), KBHitSortCharge());

    for (Int_t iHit = 0; iHit < numCandHits; iHit++) {
      KBTpcHit *candHit = fCandHits -> back();
      fCandHits -> pop_back();
      TVector3 pos = candHit -> GetPosition();

      Double_t quality = 0; 
      if (CheckHitOwner(candHit) < 0) 
        quality = Correlate(track, candHit, rScale);

      if (quality > 0) {
        track -> AddHit(candHit);
        track -> Fit();
        foundHit = true;
      } else
        fBadHits -> push_back(candHit);
    }
  }

  if (foundHit) {
    extrapolationLength = 10; 
    if (helicity != track -> Helicity())
      tailToHead = !tailToHead;
  }
  else {
    extrapolationLength += 10; 
    if (extrapolationLength > 3 * track -> TrackLength()) {
      return false;
    }
  }

#ifdef PRINT_PROCESS_SUMMARY
  kb_info << "out" << endl;
#endif
  return true;
}

Double_t LHTrackFinder::Continuity(KBHelixTrack *track)
{
  Int_t numHits = track -> GetNumHits();
#ifdef CONTINUITY_DEBUGGER
  kb_warning << "Checking continuity for Track:" << track -> GetTrackID() << " (# of hits = " << numHits << ")" << endl;
#endif
  if (numHits < 2)
    return -1;

  track -> SortHits();

  Double_t total = 0;
  Double_t continuous = 0;

  TVector3 qPre, mPre, qCur, mCur; // I need q(position on helix)

  KBVector3 kqPre;
  KBVector3 kqCur;

  auto trackHits = track -> GetHitArray();
  auto pPre = trackHits->at(0)->GetPosition();
  track -> ExtrapolateByMap(pPre, qPre, mPre);
  kqPre = KBVector3(qPre,fReferenceAxis);

  auto axis1 = fPadPlane -> GetAxis1();
  auto axis2 = fPadPlane -> GetAxis2();

  for (auto iHit = 1; iHit < numHits; iHit++)
  {
    auto pCur = trackHits->at(iHit)->GetPosition();
    track -> ExtrapolateByMap(pCur, qCur, mCur);

    kqCur = KBVector3(qCur,fReferenceAxis);

    auto val1 = kqCur.At(axis1) - kqPre.At(axis1);
    auto val2 = kqCur.At(axis2) - kqPre.At(axis2);

    auto length = sqrt(val1*val1 + val2*val2);

    total += length;
#ifdef CONTINUITY_DEBUGGER
    kb_warning << "length between Hit:" << iHit << ", Hit:" << iHit-1 << " is " << length << endl;
#endif
    if (length <= 1.2 * fPadPlane -> PadDisplacement())
      continuous += length;

#ifdef CONTINUITY_DEBUGGER
    kb_warning << continuous << " / " << total << endl;
#endif

    kqPre = kqCur;
  }

  return continuous/total;
}

bool LHTrackFinder::TrackQualityCheck(KBHelixTrack *track)
{
  Double_t continuity = Continuity(track);
#ifdef PRINT_PROCESS_SUMMARY
  //track -> Print(">");
  //KBRun::GetRun() -> Terminate(this);
#endif
  if (continuity < .6) {
    if (track -> TrackLength() * continuity < 500)
      return false;
  }

  if (track -> GetHelixRadius() < 25)
    return false;

  return true;
}
