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

/// \file NoiseSuppression.h
/// \author Felix Weiglhofer

#ifndef O2_GPU_NOISE_SUPPRESSION_H
#define O2_GPU_NOISE_SUPPRESSION_H

#include "clusterFinderDefs.h"
#include "GPUGeneralKernels.h"
#include "GPUConstantMem.h"
#include "GPUTPCClusterFinder.h"
#include "Array2D.h"
#include "PackedCharge.h"

namespace GPUCA_NAMESPACE::gpu
{

struct ChargePos;

class GPUTPCCFNoiseSuppression : public GPUKernelTemplate
{
 public:
  enum K : int32_t {
    noiseSuppression = 0,
    updatePeaks = 1,
  };
  static constexpr size_t SCRATCH_PAD_WORK_GROUP_SIZE = GPUCA_GET_THREAD_COUNT(GPUCA_LB_GPUTPCCFNoiseSuppression);
  struct GPUSharedMemory {
    ChargePos posBcast[SCRATCH_PAD_WORK_GROUP_SIZE];
    PackedCharge buf[SCRATCH_PAD_WORK_GROUP_SIZE * SCRATCH_PAD_NOISE_N];
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

  template <int32_t iKernel = defaultKernel, typename... Args>
  GPUd() static void Thread(int32_t nBlocks, int32_t nThreads, int32_t iBlock, int32_t iThread, GPUSharedMemory& smem, processorType& clusterer, Args... args);

 private:
  static GPUd() void noiseSuppressionImpl(int32_t, int32_t, int32_t, int32_t, GPUSharedMemory&, const GPUSettingsRec&, const Array2D<PackedCharge>&, const Array2D<uint8_t>&, const ChargePos*, const uint32_t, uint8_t*);

  static GPUd() void updatePeaksImpl(int32_t, int32_t, int32_t, int32_t, const ChargePos*, const uint8_t*, const uint32_t, Array2D<uint8_t>&);

  static GPUdi() void checkForMinima(const float, const float, const float, PackedCharge, int32_t, uint64_t*, uint64_t*);

  static GPUdi() void findMinima(const PackedCharge*, const uint16_t, const int32_t, int32_t, const float, const float, const float, uint64_t*, uint64_t*);

  static GPUdi() void findPeaks(const uint8_t*, const uint16_t, const int32_t, int32_t, uint64_t*);

  static GPUdi() bool keepPeak(uint64_t, uint64_t);

  static GPUd() void findMinimaAndPeaks(const Array2D<PackedCharge>&, const Array2D<uint8_t>&, const GPUSettingsRec&, float, const ChargePos&, ChargePos*, PackedCharge*, uint64_t*, uint64_t*, uint64_t*);
};

} // namespace GPUCA_NAMESPACE::gpu

#endif
