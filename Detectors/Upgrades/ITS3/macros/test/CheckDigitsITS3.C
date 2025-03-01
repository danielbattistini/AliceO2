// Copyright 2019-2020 CERN and copyright holders of ALICE O2.
// See https://alice-o2.web.cern.ch/copyright for details of the copyright holders.
// All rights not expressly granted are reserved.
//
// This software is distributed under the terms of the GNU General Public
// License v3 (GPL Version 3), copied verbatim in the file "COPYING".
//
// In applying this license CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.

/// \file CheckDigitsITS3.C
/// \brief Simple macro to check ITS3 digits

#if !defined(__CLING__) || defined(__ROOTCLING__)
#include <TCanvas.h>
#include <TFile.h>
#include <TH2F.h>
#include <TNtuple.h>
#include <TString.h>
#include <TTree.h>
#include <TROOT.h>
#include <TStyle.h>
#include <TPaveText.h>

#include <vector>
#define ENABLE_UPGRADES
#include "ITSBase/GeometryTGeo.h"
#include "DataFormatsITSMFT/Digit.h"
#include "ITS3Base/SegmentationSuperAlpide.h"
#include "ITSMFTBase/SegmentationAlpide.h"
#include "ITSMFTSimulation/Hit.h"
#include "MathUtils/Utils.h"
#include "SimulationDataFormat/ConstMCTruthContainer.h"
#include "SimulationDataFormat/IOMCTruthContainerView.h"
#include "SimulationDataFormat/MCCompLabel.h"
#include "DetectorsBase/GeometryManager.h"
#include "ITS3Base/SpecsV2.h"
#include "DataFormatsITSMFT/ROFRecord.h"
#include "fairlogger/Logger.h"
#endif

void CheckDigitsITS3(std::string digifile = "it3digits.root", std::string hitfile = "o2sim_HitsIT3.root", std::string inputGeom = "", bool batch = false)
{
  gROOT->SetBatch(batch);
  gStyle->SetPalette(kRainBow);

  using namespace o2::base;
  namespace its3 = o2::its3;
  using o2::itsmft::Digit;
  using o2::itsmft::Hit;

  using o2::itsmft::SegmentationAlpide;

  TFile* f = TFile::Open("CheckDigits.root", "recreate");
  TNtuple* nt = new TNtuple("ntd", "digit ntuple", "id:x:y:z:rowD:colD:rowH:colH:xlH:zlH:xlcH:zlcH:dx:dz");

  // Geometry
  o2::base::GeometryManager::loadGeometry(inputGeom);
  auto* gman = o2::its::GeometryTGeo::Instance();
  gman->fillMatrixCache(o2::math_utils::bit2Mask(o2::math_utils::TransformType::L2G));

  // Hits
  TFile* hitFile = TFile::Open(hitfile.data());
  TTree* hitTree = (TTree*)hitFile->Get("o2sim");
  int nevH = hitTree->GetEntries(); // hits are stored as one event per entry
  std::vector<std::vector<o2::itsmft::Hit>*> hitArray(nevH, nullptr);
  std::vector<std::unordered_map<uint64_t, int>> mc2hitVec(nevH);

  // Digits
  TFile* digFile = TFile::Open(digifile.data());
  TTree* digTree = (TTree*)digFile->Get("o2sim");

  std::vector<o2::itsmft::Digit>* digArr = nullptr;
  digTree->SetBranchAddress("IT3Digit", &digArr);

  o2::dataformats::IOMCTruthContainerView* plabels = nullptr;
  digTree->SetBranchAddress("IT3DigitMCTruth", &plabels);

  int nevD = digTree->GetEntries(); // digits in cont. readout may be grouped as few events per entry

  int lastReadHitEv = -1;

  int nDigitReadIB{0}, nDigitReadOB{0};
  int nDigitFilledIB{0}, nDigitFilledOB{0};

  // Get Read Out Frame arrays
  std::vector<o2::itsmft::ROFRecord>* ROFRecordArrray = nullptr;
  digTree->SetBranchAddress("IT3DigitROF", &ROFRecordArrray);
  std::vector<o2::itsmft::ROFRecord>& ROFRecordArrrayRef = *ROFRecordArrray;

  std::vector<o2::itsmft::MC2ROFRecord>* MC2ROFRecordArrray = nullptr;
  digTree->SetBranchAddress("IT3DigitMC2ROF", &MC2ROFRecordArrray);
  std::vector<o2::itsmft::MC2ROFRecord>& MC2ROFRecordArrrayRef = *MC2ROFRecordArrray;

  digTree->GetEntry(0);

  int nROFRec = (int)ROFRecordArrrayRef.size();
  std::vector<int> mcEvMin(nROFRec, hitTree->GetEntries());
  std::vector<int> mcEvMax(nROFRec, -1);
  o2::dataformats::ConstMCTruthContainer<o2::MCCompLabel> labels;
  plabels->copyandflatten(labels);
  delete plabels;

  LOGP(debug, "Build min and max MC events used by each ROF");
  for (int imc = MC2ROFRecordArrrayRef.size(); imc--;) {
    const auto& mc2rof = MC2ROFRecordArrrayRef[imc];
    /* LOGP(debug, "MCRecord: {}", mc2rof.asString()); */

    if (mc2rof.rofRecordID < 0) {
      continue; // this MC event did not contribute to any ROF
    }

    for (int irfd = mc2rof.maxROF - mc2rof.minROF + 1; irfd--;) {

      int irof = mc2rof.rofRecordID + irfd;

      if (irof >= nROFRec) {
        LOGP(error, "ROF={} from MC2ROF record is >= N ROFs={}", irof, nROFRec);
      }
      if (mcEvMin[irof] > imc) {
        mcEvMin[irof] = imc;
      }
      if (mcEvMax[irof] < imc) {
        mcEvMax[irof] = imc;
      }
    }
  }

  LOGP(debug, "LOOP on: ROFRecord array");
  unsigned int rofIndex = 0;
  unsigned int rofNEntries = 0;
  for (unsigned int iROF = 0; iROF < ROFRecordArrrayRef.size(); iROF++) {

    rofIndex = ROFRecordArrrayRef[iROF].getFirstEntry();
    rofNEntries = ROFRecordArrrayRef[iROF].getNEntries();

    // >> read and map MC events contributing to this ROF
    for (int im = mcEvMin[iROF]; im <= mcEvMax[iROF]; im++) {

      if (hitArray[im] == nullptr) {

        hitTree->SetBranchAddress("IT3Hit", &hitArray[im]);
        hitTree->GetEntry(im);

        auto& mc2hit = mc2hitVec[im];

        for (size_t ih = hitArray[im]->size(); ih--;) {
          const auto& hit = (*hitArray[im])[ih];
          uint64_t key = (uint64_t(hit.GetTrackID()) << 32) + hit.GetDetectorID();
          mc2hit.emplace(key, ih);
        }
      }
    }

    LOGP(debug, "  `-> LOOP on: Digits array(size={}) starting at ROFIndex={} to {}", digArr->size(), rofIndex, rofIndex + rofNEntries);
    for (unsigned int iDigit = rofIndex; iDigit < rofIndex + rofNEntries; iDigit++) {
      int ix = (*digArr)[iDigit].getRow(), iz = (*digArr)[iDigit].getColumn();
      auto chipID = (*digArr)[iDigit].getChipIndex();
      auto layer = its3::constants::detID::getDetID2Layer(chipID);
      bool isIB{its3::constants::detID::isDetITS3(chipID)};
      float x{0.f}, y{0.f}, z{0.f};
      (isIB) ? ++nDigitReadIB : ++nDigitReadOB;

      if (isIB) {
        // ITS3 IB
        float xFlat{0.f}, yFlat{0.f};
        its3::SuperSegmentations[layer].detectorToLocal(ix, iz, xFlat, z);
        its3::SuperSegmentations[layer].flatToCurved(xFlat, 0., x, y);
      } else {
        // ITS2 OB
        SegmentationAlpide::detectorToLocal(ix, iz, x, z);
      }

      o2::math_utils::Point3D<double> locD(x, y, z);
      auto lab = (labels.getLabels(iDigit))[0];
      int trID = lab.getTrackID();
      if (!lab.isValid()) { // not noise
        continue;
      }

      // get MC info
      uint64_t key = (uint64_t(trID) << 32) + chipID;
      const auto* mc2hit = &mc2hitVec[lab.getEventID()];
      const auto& hitEntry = mc2hit->find(key);
      if (hitEntry == mc2hit->end()) {
        LOGP(error, "Failed to find MC hit entry for Tr {} chipID {}", trID, chipID);
        continue;
      }

      auto gloD = gman->getMatrixL2G(chipID)(locD); // convert to global

      ////// HITS
      Hit& hit = (*hitArray[lab.getEventID()])[hitEntry->second];

      auto xyzLocE = gman->getMatrixL2G(chipID) ^ (hit.GetPos()); // inverse conversion from global to local
      auto xyzLocS = gman->getMatrixL2G(chipID) ^ (hit.GetPosStart());
      o2::math_utils::Vector3D<float> xyzLocM;
      xyzLocM.SetCoordinates(0.5 * (xyzLocE.X() + xyzLocS.X()), 0.5 * (xyzLocE.Y() + xyzLocS.Y()), 0.5 * (xyzLocE.Z() + xyzLocS.Z()));
      float xlc = 0., zlc = 0.;
      int row = 0, col = 0;

      if (isIB) {
        float xFlat{0.}, yFlat{0.};
        its3::SuperSegmentations[layer].curvedToFlat(xyzLocM.X(), xyzLocM.Y(), xFlat, yFlat);
        xyzLocM.SetCoordinates(xFlat, yFlat, xyzLocM.Z());
        its3::SuperSegmentations[layer].curvedToFlat(locD.X(), locD.Y(), xFlat, yFlat);
        locD.SetCoordinates(xFlat, yFlat, locD.Z());
        if (auto v1 = !its3::SuperSegmentations[layer].localToDetector(xyzLocM.X(), xyzLocM.Z(), row, col),
            v2 = !its3::SuperSegmentations[layer].detectorToLocal(row, col, xlc, zlc);
            v1 || v2) {
          continue;
        }
      } else {
        if (auto v1 = !SegmentationAlpide::localToDetector(xyzLocM.X(), xyzLocM.Z(), row, col),
            v2 = !SegmentationAlpide::detectorToLocal(row, col, xlc, zlc);
            v1 || v2) {
          continue;
        }
      }

      nt->Fill(chipID, gloD.X(), gloD.Y(), gloD.Z(), ix, iz, row, col, xyzLocM.X(), xyzLocM.Z(), xlc, zlc, xyzLocM.X() - locD.X(), xyzLocM.Z() - locD.Z());

      (isIB) ? ++nDigitFilledIB : ++nDigitFilledOB;
    } // end loop on digits array
  }   // end loop on ROFRecords array

  auto canvXY = new TCanvas("canvXY", "", 1600, 1600);
  canvXY->Divide(2, 2);
  canvXY->cd(1);
  nt->Draw("y:x>>h_y_vs_x_IB(1000, -5, 5, 1000, -5, 5)", "id < 3456", "colz");
  canvXY->cd(2);
  nt->Draw("y:z>>h_y_vs_z_IB(1000, -15, 15, 1000, -5, 5)", "id < 3456", "colz");
  canvXY->cd(3);
  nt->Draw("y:x>>h_y_vs_x_OB(1000, -50, 50, 1000, -50, 50)", "id >= 3456", "colz");
  canvXY->cd(4);
  nt->Draw("y:z>>h_y_vs_z_OB(1000, -100, 100, 1000, -50, 50)", "id >= 3456", "colz");
  canvXY->SaveAs("it3digits_y_vs_x_vs_z.pdf");

  auto canvdXdZ = new TCanvas("canvdXdZ", "", 1600, 800);
  canvdXdZ->Divide(2, 2);
  canvdXdZ->cd(1);
  nt->Draw("dx:dz>>h_dx_vs_dz_IB(500, -0.02, 0.02, 500, -0.01, 0.01)", "id < 3456", "colz");
  auto h = (TH2F*)gPad->GetPrimitive("h_dx_vs_dz_IB");
  Info("IB", "RMS(dx)=%.1f mu", h->GetRMS(2) * 1e4);
  Info("IB", "RMS(dz)=%.1f mu", h->GetRMS(1) * 1e4);
  canvdXdZ->cd(2);
  nt->Draw("dx:dz>>h_dx_vs_dz_OB(500, -0.02, 0.02, 500, -0.02, 0.02)", "id >= 3456", "colz");
  h = (TH2F*)gPad->GetPrimitive("h_dx_vs_dz_OB");
  Info("OB", "RMS(dx)=%.1f mu", h->GetRMS(2) * 1e4);
  Info("OB", "RMS(dz)=%.1f mu", h->GetRMS(1) * 1e4);
  canvdXdZ->cd(3);
  nt->Draw("dx:dz>>h_dx_vs_dz_IB_z(500, -0.005, 0.005, 500, -0.005, 0.005)", "id < 3456 && abs(z)<2", "colz");
  h = (TH2F*)gPad->GetPrimitive("h_dx_vs_dz_IB_z");
  Info("IB |z|<2", "RMS(dx)=%.1f mu", h->GetRMS(2) * 1e4);
  Info("IB |z|<2", "RMS(dz)=%.1f mu", h->GetRMS(1) * 1e4);
  canvdXdZ->cd(4);
  nt->Draw("dx:dz>>h_dx_vs_dz_OB_z(500, -0.005, 0.005, 500, -0.005, 0.005)", "id >= 3456 && abs(z)<2", "colz");
  h = (TH2F*)gPad->GetPrimitive("h_dx_vs_dz_OB_z");
  Info("OB |z|<2", "RMS(dx)=%.1f mu", h->GetRMS(2) * 1e4);
  Info("OB |z|<2", "RMS(dz)=%.1f mu", h->GetRMS(1) * 1e4);
  canvdXdZ->SaveAs("it3digits_dx_vs_dz.pdf");

  f->Write();
  f->Close();
  Info("EXIT", "read %d filled %d in IB\n", nDigitReadIB, nDigitFilledIB);
  Info("EXIT", "read %d filled %d in OB\n", nDigitReadOB, nDigitFilledOB);
}
