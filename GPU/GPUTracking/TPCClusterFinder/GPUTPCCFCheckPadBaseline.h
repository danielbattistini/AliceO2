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

/// \file GPUTPCCFCheckPadBaseline.h
/// \author Felix Weiglhofer

#ifndef O2_GPU_GPU_TPC_CF_CHECK_PAD_BASELINE_H
#define O2_GPU_GPU_TPC_CF_CHECK_PAD_BASELINE_H

#include "GPUGeneralKernels.h"
#include "GPUConstantMem.h"

#include "clusterFinderDefs.h"

namespace GPUCA_NAMESPACE::gpu
{

class GPUTPCCFCheckPadBaseline : public GPUKernelTemplate
{

 public:
  enum {
    PadsPerCacheline = 8,
    TimebinsPerCacheline = 4,
    NumOfCachedTimebins = GPUCA_GET_THREAD_COUNT(GPUCA_LB_GPUTPCCFCheckPadBaseline) / PadsPerCacheline,
  };

  struct GPUSharedMemory {
    tpccf::Charge charges[PadsPerCacheline][NumOfCachedTimebins];
  };

#ifdef GPUCA_HAVE_O2HEADERS
  typedef GPUTPCClusterFinder processorType;
  GPUhdi() static processorType* Processor(GPUConstantMem& processors)
  {
    return processors.tpcClusterer;
  }
#endif

  GPUhdi() constexpr static GPUDataTypes::RecoStep GetRecoStep()
  {
    return GPUDataTypes::RecoStep::TPCClusterFinding;
  }

  template <int32_t iKernel = defaultKernel>
  GPUd() static void Thread(int32_t nBlocks, int32_t nThreads, int32_t iBlock, int32_t iThread, GPUSharedMemory& smem, processorType& clusterer);

 private:
  GPUd() static ChargePos padToChargePos(int32_t& pad, const GPUTPCClusterFinder&);
  GPUd() static void updatePadBaseline(int32_t pad, const GPUTPCClusterFinder&, int32_t totalCharges, int32_t consecCharges, tpccf::Charge maxCharge);
};

} // namespace GPUCA_NAMESPACE::gpu

#endif
