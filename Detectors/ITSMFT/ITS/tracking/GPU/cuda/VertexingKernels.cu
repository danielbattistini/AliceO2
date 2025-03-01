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
///

#include <cuda_runtime.h>
#include <cub/cub.cuh>

#include "ITStrackingGPU/VertexingKernels.h"

namespace o2
{
namespace its
{
using constants::its::VertexerHistogramVolume;
using constants::math::TwoPi;
using gpu::utils::checkGPUError;
using math_utils::getNormalizedPhi;
using namespace constants::its2;

namespace gpu
{
template <TrackletMode Mode>
void trackletFinderHandler(const Cluster* clustersNextLayer,    // 0 2
                           const Cluster* clustersCurrentLayer, // 1 1
                           const int* sizeNextLClusters,
                           const int* sizeCurrentLClusters,
                           const int* nextIndexTables,
                           Tracklet* Tracklets,
                           int* foundTracklets,
                           const IndexTableUtils* utils,
                           const unsigned int startRofId,
                           const unsigned int rofSize,
                           const float phiCut,
                           const unsigned int maxTrackletsPerCluster,
                           const int nBlocks,
                           const int nThreads)
{
  gpu::trackleterKernelMultipleRof<Mode><<<nBlocks, nThreads>>>(
    clustersNextLayer,       // const Cluster* clustersNextLayer,    // 0 2
    clustersCurrentLayer,    // const Cluster* clustersCurrentLayer, // 1 1
    sizeNextLClusters,       // const int* sizeNextLClusters,
    sizeCurrentLClusters,    // const int* sizeCurrentLClusters,
    nextIndexTables,         // const int* nextIndexTables,
    Tracklets,               // Tracklet* Tracklets,
    foundTracklets,          // int* foundTracklets,
    utils,                   // const IndexTableUtils* utils,
    startRofId,              // const unsigned int startRofId,
    rofSize,                 // const unsigned int rofSize,
    phiCut,                  // const float phiCut,
    maxTrackletsPerCluster); // const unsigned int maxTrackletsPerCluster = 1e2
}
/*
GPUd() float smallestAngleDifference(float a, float b)
{
  float diff = fmod(b - a + constants::math::Pi, constants::math::TwoPi) - constants::math::Pi;
  return (diff < -constants::math::Pi) ? diff + constants::math::TwoPi : ((diff > constants::math::Pi) ? diff - constants::math::TwoPi : diff);
}

GPUd() const int4 getBinsRect(const Cluster& currentCluster, const int layerIndex,
                              const float z1, float maxdeltaz, float maxdeltaphi)
{
  const float zRangeMin = z1 - maxdeltaz;
  const float phiRangeMin = currentCluster.phi - maxdeltaphi;
  const float zRangeMax = z1 + maxdeltaz;
  const float phiRangeMax = currentCluster.phi + maxdeltaphi;

  if (zRangeMax < -LayersZCoordinate()[layerIndex + 1] ||
      zRangeMin > LayersZCoordinate()[layerIndex + 1] || zRangeMin > zRangeMax) {

    return getEmptyBinsRect();
  }

  return int4{o2::gpu::GPUCommonMath::Max(0, getZBinIndex(layerIndex + 1, zRangeMin)),
              getPhiBinIndex(phiRangeMin),
              o2::gpu::GPUCommonMath::Min(ZBins - 1, getZBinIndex(layerIndex + 1, zRangeMax)),
              getPhiBinIndex(phiRangeMax)};
}

GPUh() void gpuThrowOnError()
{
  cudaError_t error = cudaGetLastError();

  if (error != cudaSuccess) {
    std::ostringstream errorString{};
    errorString << GPU_ARCH << " API returned error  [" << cudaGetErrorString(error) << "] (code " << error << ")" << std::endl;
    throw std::runtime_error{errorString.str()};
  }
}

template <typename... Args>
GPUd() void printOnThread(const unsigned int tId, const char* str, Args... args)
{
  if (blockIdx.x * blockDim.x + threadIdx.x == tId) {
    printf(str, args...);
  }
}

template <typename... Args>
GPUd() void printOnBlock(const unsigned int bId, const char* str, Args... args)
{
  if (blockIdx.x == bId && threadIdx.x == 0) {
    printf(str, args...);
  }
}

GPUg() void printBufferOnThread(const int* v, unsigned int size, const int len = 150, const unsigned int tId = 0)
{
  if (blockIdx.x * blockDim.x + threadIdx.x == tId) {
    for (int i{0}; i < size; ++i) {
      if (!(i % len)) {
        printf("\n start: ===>%d/%d\t", i, (int)size);
      }
      printf("%d\t", v[i]);
    }
    printf("\n");
  }
}

GPUg() void printBufferOnThreadF(const float* v, unsigned int size, const unsigned int tId = 0)
{
  if (blockIdx.x * blockDim.x + threadIdx.x == tId) {
    printf("vector :");
    for (int i{0}; i < size; ++i) {
      printf("%.9f\t", v[i]);
    }
    printf("\n");
  }
}

GPUg() void resetTrackletsKernel(Tracklet* tracklets, const int nTracklets)
{
  for (int iCurrentLayerClusterIndex = blockIdx.x * blockDim.x + threadIdx.x; iCurrentLayerClusterIndex < nTracklets; iCurrentLayerClusterIndex += blockDim.x * gridDim.x) {
    new (tracklets + iCurrentLayerClusterIndex) Tracklet{};
  }
}

GPUg() void dumpFoundTrackletsKernel(const Tracklet* tracklets, const int* nTracklet, const unsigned int nClustersMiddleLayer, const int maxTrackletsPerCluster)
{
  for (int iCurrentLayerClusterIndex = blockIdx.x * blockDim.x + threadIdx.x; iCurrentLayerClusterIndex < nClustersMiddleLayer; iCurrentLayerClusterIndex += blockDim.x * gridDim.x) {
    const int stride{iCurrentLayerClusterIndex * maxTrackletsPerCluster};
    for (int iTracklet{0}; iTracklet < nTracklet[iCurrentLayerClusterIndex]; ++iTracklet) {
      auto& t = tracklets[stride + iTracklet];
      t.dump();
    }
  }
}

GPUg() void dumpMaximaKernel(const cub::KeyValuePair<int, int>* tmpVertexBins, const int threadId)
{
  if (blockIdx.x * blockDim.x + threadIdx.x == threadId) {
    printf("XmaxBin: %d at index: %d | YmaxBin: %d at index: %d | ZmaxBin: %d at index: %d\n",
           tmpVertexBins[0].value, tmpVertexBins[0].key,
           tmpVertexBins[1].value, tmpVertexBins[1].key,
           tmpVertexBins[2].value, tmpVertexBins[2].key);
  }
}

template <TrackletMode Mode>
GPUg() void trackleterKernelSingleRof(
  const Cluster* clustersNextLayer,    // 0 2
  const Cluster* clustersCurrentLayer, // 1 1
  const int sizeNextLClusters,
  const int sizeCurrentLClusters,
  const int* indexTableNext,
  const float phiCut,
  Tracklet* Tracklets,
  int* foundTracklets,
  const IndexTableUtils* utils,
  const short rofId,
  const unsigned int maxTrackletsPerCluster = 1e2)
{
  const int phiBins{utils->getNphiBins()};
  const int zBins{utils->getNzBins()};
  // loop on layer1 clusters
  for (int iCurrentLayerClusterIndex = blockIdx.x * blockDim.x + threadIdx.x; iCurrentLayerClusterIndex < sizeCurrentLClusters; iCurrentLayerClusterIndex += blockDim.x * gridDim.x) {
    if (iCurrentLayerClusterIndex < sizeCurrentLClusters) {
      unsigned int storedTracklets{0};
      const unsigned int stride{iCurrentLayerClusterIndex * maxTrackletsPerCluster};
      const Cluster& currentCluster = clustersCurrentLayer[iCurrentLayerClusterIndex];
      const int4 selectedBinsRect{VertexerTraits::getBinsRect(currentCluster, (int)Mode, 0.f, 50.f, phiCut / 2, *utils)};
      if (selectedBinsRect.x != 0 || selectedBinsRect.y != 0 || selectedBinsRect.z != 0 || selectedBinsRect.w != 0) {
        int phiBinsNum{selectedBinsRect.w - selectedBinsRect.y + 1};
        if (phiBinsNum < 0) {
          phiBinsNum += phiBins;
        }
        // loop on phi bins next layer
        for (unsigned int iPhiBin{(unsigned int)selectedBinsRect.y}, iPhiCount{0}; iPhiCount < (unsigned int)phiBinsNum; iPhiBin = ++iPhiBin == phiBins ? 0 : iPhiBin, iPhiCount++) {
          const int firstBinIndex{utils->getBinIndex(selectedBinsRect.x, iPhiBin)};
          const int firstRowClusterIndex{indexTableNext[firstBinIndex]};
          const int maxRowClusterIndex{indexTableNext[firstBinIndex + zBins]};
          // loop on clusters next layer
          for (int iNextLayerClusterIndex{firstRowClusterIndex}; iNextLayerClusterIndex < maxRowClusterIndex && iNextLayerClusterIndex < sizeNextLClusters; ++iNextLayerClusterIndex) {
            const Cluster& nextCluster = clustersNextLayer[iNextLayerClusterIndex];
            if (o2::gpu::GPUCommonMath::Abs(currentCluster.phi - nextCluster.phi) < phiCut) {
              if (storedTracklets < maxTrackletsPerCluster) {
                if constexpr (Mode == TrackletMode::Layer0Layer1) {
                  new (Tracklets + stride + storedTracklets) Tracklet{iNextLayerClusterIndex, iCurrentLayerClusterIndex, nextCluster, currentCluster, rofId, rofId};
                } else {
                  new (Tracklets + stride + storedTracklets) Tracklet{iCurrentLayerClusterIndex, iNextLayerClusterIndex, currentCluster, nextCluster, rofId, rofId};
                }
                ++storedTracklets;
              }
            }
          }
        }
      }
      foundTracklets[iCurrentLayerClusterIndex] = storedTracklets;
      if (storedTracklets >= maxTrackletsPerCluster) {
        printf("gpu tracklet finder: some lines will be left behind for cluster %d. valid: %u max: %zu\n", iCurrentLayerClusterIndex, storedTracklets, maxTrackletsPerCluster);
      }
    }
  }
}

template <TrackletMode Mode>
GPUg() void trackleterKernelMultipleRof(
  const Cluster* clustersNextLayer,    // 0 2
  const Cluster* clustersCurrentLayer, // 1 1
  const int* sizeNextLClusters,
  const int* sizeCurrentLClusters,
  const int* nextIndexTables,
  Tracklet* Tracklets,
  int* foundTracklets,
  const IndexTableUtils* utils,
  const short startRofId,
  const short rofSize,
  const float phiCut,
  const unsigned int maxTrackletsPerCluster = 1e2)
{
  const int phiBins{utils->getNphiBins()};
  const int zBins{utils->getNzBins()};
  for (auto iRof{blockIdx.x}; iRof < rofSize; iRof += gridDim.x) {
    short rof = static_cast<short>(iRof) + startRofId;
    auto* clustersNextLayerRof = clustersNextLayer + (sizeNextLClusters[rof] - sizeNextLClusters[startRofId]);
    auto* clustersCurrentLayerRof = clustersCurrentLayer + (sizeCurrentLClusters[rof] - sizeCurrentLClusters[startRofId]);
    auto nClustersNextLayerRof = sizeNextLClusters[rof + 1] - sizeNextLClusters[rof];
    auto nClustersCurrentLayerRof = sizeCurrentLClusters[rof + 1] - sizeCurrentLClusters[rof];
    auto* indexTableNextRof = nextIndexTables + iRof * (phiBins * zBins + 1);
    auto* TrackletsRof = Tracklets + (sizeCurrentLClusters[rof] - sizeCurrentLClusters[startRofId]) * maxTrackletsPerCluster;
    auto* foundTrackletsRof = foundTracklets + (sizeCurrentLClusters[rof] - sizeCurrentLClusters[startRofId]);

    // single rof loop on layer1 clusters
    for (int iCurrentLayerClusterIndex = threadIdx.x; iCurrentLayerClusterIndex < nClustersCurrentLayerRof; iCurrentLayerClusterIndex += blockDim.x) {
      unsigned int storedTracklets{0};
      const unsigned int stride{iCurrentLayerClusterIndex * maxTrackletsPerCluster};
      const Cluster& currentCluster = clustersCurrentLayerRof[iCurrentLayerClusterIndex];
      const int4 selectedBinsRect{VertexerTraits::getBinsRect(currentCluster, (int)Mode, 0.f, 50.f, phiCut / 2, *utils)};
      if (selectedBinsRect.x != 0 || selectedBinsRect.y != 0 || selectedBinsRect.z != 0 || selectedBinsRect.w != 0) {
        int phiBinsNum{selectedBinsRect.w - selectedBinsRect.y + 1};
        if (phiBinsNum < 0) {
          phiBinsNum += phiBins;
        }
        // loop on phi bins next layer
        for (unsigned int iPhiBin{(unsigned int)selectedBinsRect.y}, iPhiCount{0}; iPhiCount < (unsigned int)phiBinsNum; iPhiBin = ++iPhiBin == phiBins ? 0 : iPhiBin, iPhiCount++) {
          const int firstBinIndex{utils->getBinIndex(selectedBinsRect.x, iPhiBin)};
          const int firstRowClusterIndex{indexTableNextRof[firstBinIndex]};
          const int maxRowClusterIndex{indexTableNextRof[firstBinIndex + zBins]};
          // loop on clusters next layer
          for (int iNextLayerClusterIndex{firstRowClusterIndex}; iNextLayerClusterIndex < maxRowClusterIndex && iNextLayerClusterIndex < nClustersNextLayerRof; ++iNextLayerClusterIndex) {
            const Cluster& nextCluster = clustersNextLayerRof[iNextLayerClusterIndex];
            if (o2::gpu::GPUCommonMath::Abs(smallestAngleDifference(currentCluster.phi, nextCluster.phi)) < phiCut) {
              if (storedTracklets < maxTrackletsPerCluster) {
                if constexpr (Mode == TrackletMode::Layer0Layer1) {
                  new (TrackletsRof + stride + storedTracklets) Tracklet{iNextLayerClusterIndex, iCurrentLayerClusterIndex, nextCluster, currentCluster, rof, rof};
                } else {
                  new (TrackletsRof + stride + storedTracklets) Tracklet{iCurrentLayerClusterIndex, iNextLayerClusterIndex, currentCluster, nextCluster, rof, rof};
                }
                ++storedTracklets;
              }
            }
          }
        }
      }
      foundTrackletsRof[iCurrentLayerClusterIndex] = storedTracklets;
      // if (storedTracklets >= maxTrackletsPerCluster && storedTracklets - maxTrackletsPerCluster < 5) {
      //   printf("gpu tracklet finder: some lines will be left behind for cluster %d in rof: %d. valid: %u max: %lu (suppressing after 5 msgs)\n", iCurrentLayerClusterIndex, rof, storedTracklets, maxTrackletsPerCluster);
      // }
    }
  }
}

template <bool initRun>
GPUg() void trackletSelectionKernelSingleRof(
  const Cluster* clusters0,
  const Cluster* clusters1,
  const unsigned int nClustersMiddleLayer,
  Tracklet* tracklets01,
  Tracklet* tracklets12,
  const int* nFoundTracklet01,
  const int* nFoundTracklet12,
  unsigned char* usedTracklets,
  Line* lines,
  int* nFoundLines,
  int* nExclusiveFoundLines,
  const int maxTrackletsPerCluster = 1e2,
  const float tanLambdaCut = 0.025f,
  const float phiCut = 0.002f)
{
  for (int iCurrentLayerClusterIndex = blockIdx.x * blockDim.x + threadIdx.x; iCurrentLayerClusterIndex < nClustersMiddleLayer; iCurrentLayerClusterIndex += blockDim.x * gridDim.x) {
    const int stride{iCurrentLayerClusterIndex * maxTrackletsPerCluster};
    int validTracklets{0};
    for (int iTracklet12{0}; iTracklet12 < nFoundTracklet12[iCurrentLayerClusterIndex]; ++iTracklet12) {
      for (int iTracklet01{0}; iTracklet01 < nFoundTracklet01[iCurrentLayerClusterIndex] && validTracklets < maxTrackletsPerCluster; ++iTracklet01) {
        const float deltaTanLambda{o2::gpu::GPUCommonMath::Abs(tracklets01[stride + iTracklet01].tanLambda - tracklets12[stride + iTracklet12].tanLambda)};
        const float deltaPhi{o2::gpu::GPUCommonMath::Abs(smallestAngleDifference(tracklets01[stride + iTracklet01].phi, tracklets12[stride + iTracklet12].phi))};
        if (!usedTracklets[stride + iTracklet01] && deltaTanLambda < tanLambdaCut && deltaPhi < phiCut && validTracklets != maxTrackletsPerCluster) {
          usedTracklets[stride + iTracklet01] = true;
          if constexpr (!initRun) {
            new (lines + nExclusiveFoundLines[iCurrentLayerClusterIndex] + validTracklets) Line{tracklets01[stride + iTracklet01], clusters0, clusters1};
          }
          ++validTracklets;
        }
      }
    }
    if constexpr (initRun) {
      nFoundLines[iCurrentLayerClusterIndex] = validTracklets;
      if (validTracklets >= maxTrackletsPerCluster) {
        printf("gpu tracklet selection: some lines will be left behind for cluster %d. valid: %d max: %d\n", iCurrentLayerClusterIndex, validTracklets, maxTrackletsPerCluster);
      }
    }
  }
}

template <bool initRun>
GPUg() void trackletSelectionKernelMultipleRof(
  const Cluster* clusters0,               // Clusters on layer 0
  const Cluster* clusters1,               // Clusters on layer 1
  const int* sizeClustersL0,              // Number of clusters on layer 0 per ROF
  const int* sizeClustersL1,              // Number of clusters on layer 1 per ROF
  Tracklet* tracklets01,                  // Tracklets on layer 0-1
  Tracklet* tracklets12,                  // Tracklets on layer 1-2
  const int* nFoundTracklets01,           // Number of tracklets found on layers 0-1
  const int* nFoundTracklets12,           // Number of tracklets found on layers 1-2
  unsigned char* usedTracklets,           // Used tracklets
  Line* lines,                            // Lines
  int* nFoundLines,                       // Number of found lines
  int* nExclusiveFoundLines,              // Number of found lines exclusive scan
  const unsigned int startRofId,          // Starting ROF ID
  const unsigned int rofSize,             // Number of ROFs to consider
  const int maxTrackletsPerCluster = 1e2, // Maximum number of tracklets per cluster
  const float tanLambdaCut = 0.025f,      // Cut on tan lambda
  const float phiCut = 0.002f)            // Cut on phi
{
  for (unsigned int iRof{blockIdx.x}; iRof < rofSize; iRof += gridDim.x) {
    auto rof = iRof + startRofId;
    auto* clustersL0Rof = clusters0 + (sizeClustersL0[rof] - sizeClustersL0[startRofId]);
    auto clustersL1offsetRof = sizeClustersL1[rof] - sizeClustersL1[startRofId];
    auto* clustersL1Rof = clusters1 + clustersL1offsetRof;
    auto nClustersL1Rof = sizeClustersL1[rof + 1] - sizeClustersL1[rof];
    auto* tracklets01Rof = tracklets01 + clustersL1offsetRof * maxTrackletsPerCluster;
    auto* tracklets12Rof = tracklets12 + clustersL1offsetRof * maxTrackletsPerCluster;
    auto* foundTracklets01Rof = nFoundTracklets01 + clustersL1offsetRof;
    auto* foundTracklets12Rof = nFoundTracklets12 + clustersL1offsetRof;
    auto* usedTrackletsRof = usedTracklets + clustersL1offsetRof * maxTrackletsPerCluster;
    auto* foundLinesRof = nFoundLines + clustersL1offsetRof;
    int* nExclusiveFoundLinesRof = nullptr;
    if constexpr (!initRun) {
      nExclusiveFoundLinesRof = nExclusiveFoundLines + clustersL1offsetRof;
    }
    for (int iClusterIndexLayer1 = threadIdx.x; iClusterIndexLayer1 < nClustersL1Rof; iClusterIndexLayer1 += blockDim.x) {
      const int stride{iClusterIndexLayer1 * maxTrackletsPerCluster};
      int validTracklets{0};
      for (int iTracklet12{0}; iTracklet12 < foundTracklets12Rof[iClusterIndexLayer1]; ++iTracklet12) {
        for (int iTracklet01{0}; iTracklet01 < foundTracklets01Rof[iClusterIndexLayer1] && validTracklets < maxTrackletsPerCluster; ++iTracklet01) {
          const float deltaTanLambda{o2::gpu::GPUCommonMath::Abs(tracklets01Rof[stride + iTracklet01].tanLambda - tracklets12Rof[stride + iTracklet12].tanLambda)};
          const float deltaPhi{o2::gpu::GPUCommonMath::Abs(tracklets01Rof[stride + iTracklet01].phi - tracklets12Rof[stride + iTracklet12].phi)};
          if (!usedTrackletsRof[stride + iTracklet01] && deltaTanLambda < tanLambdaCut && deltaPhi < phiCut && validTracklets != maxTrackletsPerCluster) {
            usedTrackletsRof[stride + iTracklet01] = true;
            if constexpr (!initRun) {
              new (lines + nExclusiveFoundLinesRof[iClusterIndexLayer1] + validTracklets) Line{tracklets01Rof[stride + iTracklet01], clustersL0Rof, clustersL1Rof};
            }
            ++validTracklets;
          }
        }
      }
      if constexpr (initRun) {
        foundLinesRof[iClusterIndexLayer1] = validTracklets;
        // if (validTracklets >= maxTrackletsPerCluster) {
        // printf("gpu tracklet selection: some lines will be left behind for cluster %d. valid: %d max: %d\n", iClusterIndexLayer1, validTracklets, maxTrackletsPerCluster);
        // }
      }
    }
  } // rof loop
}

GPUg() void lineClustererMultipleRof(
  const int* sizeClustersL1,     // Number of clusters on layer 1 per ROF
  Line* lines,                   // Lines
  int* nFoundLines,              // Number of found lines
  int* nExclusiveFoundLines,     // Number of found lines exclusive scan
  int* clusteredLines,           // Clustered lines
  const unsigned int startRofId, // Starting ROF ID
  const unsigned int rofSize,    // Number of ROFs to consider // Number of found lines exclusive scan
  const float pairCut)           // Selection on line pairs
{
  for (unsigned int iRof{threadIdx.x}; iRof < rofSize; iRof += blockDim.x) {
    auto rof = iRof + startRofId;
    auto clustersL1offsetRof = sizeClustersL1[rof] - sizeClustersL1[startRofId]; // starting cluster offset for this ROF
    auto nClustersL1Rof = sizeClustersL1[rof + 1] - sizeClustersL1[rof];         // number of clusters for this ROF
    auto linesOffsetRof = nExclusiveFoundLines[clustersL1offsetRof];             // starting line offset for this ROF
    // auto* foundLinesRof = nFoundLines + clustersL1offsetRof;
    auto nLinesRof = nExclusiveFoundLines[clustersL1offsetRof + nClustersL1Rof] - linesOffsetRof;
    // printf("rof: %d -> %d lines.\n", rof, nLinesRof);
    for (int iLine1 = 0; iLine1 < nLinesRof; ++iLine1) {
      auto absLine1Index = nExclusiveFoundLines[clustersL1offsetRof] + iLine1;
      if (clusteredLines[absLine1Index] > -1) {
        continue;
      }
      for (int iLine2 = iLine1 + 1; iLine2 < nLinesRof; ++iLine2) {
        auto absLine2Index = nExclusiveFoundLines[clustersL1offsetRof] + iLine2;
        if (clusteredLines[absLine2Index] > -1) {
          continue;
        }

        if (Line::getDCA(lines[absLine1Index], lines[absLine2Index]) < pairCut) {
          ClusterLinesGPU tmpClus{lines[absLine1Index], lines[absLine2Index]};
          float tmpVertex[3];
          tmpVertex[0] = tmpClus.getVertex()[0];
          tmpVertex[1] = tmpClus.getVertex()[1];
          tmpVertex[2] = tmpClus.getVertex()[2];
          if (tmpVertex[0] * tmpVertex[0] + tmpVertex[1] * tmpVertex[1] > 4.f) { // outside the beampipe, skip it
            break;
          }
          clusteredLines[absLine1Index] = iLine1; // We set local index of first line to contribute, so we can retrieve the cluster later
          clusteredLines[absLine2Index] = iLine1;
          for (int iLine3 = 0; iLine3 < nLinesRof; ++iLine3) {
            auto absLine3Index = nExclusiveFoundLines[clustersL1offsetRof] + iLine3;
            if (clusteredLines[absLine3Index] > -1) {
              continue;
            }
            if (Line::getDistanceFromPoint(lines[absLine3Index], tmpVertex) < pairCut) {
              clusteredLines[absLine3Index] = iLine1;
            }
          }
          break;
        }
      }
    }
  } // rof loop
}

GPUg() void computeCentroidsKernel(
  Line* lines,
  int* nFoundLines,
  int* nExclusiveFoundLines,
  const unsigned int nClustersMiddleLayer,
  float* centroids,
  const float lowHistX,
  const float highHistX,
  const float lowHistY,
  const float highHistY,
  const float pairCut)
{
  const int nLines = nExclusiveFoundLines[nClustersMiddleLayer - 1] + nFoundLines[nClustersMiddleLayer - 1];
  const int maxIterations{nLines * (nLines - 1) / 2};
  for (unsigned int currentThreadIndex = blockIdx.x * blockDim.x + threadIdx.x; currentThreadIndex < maxIterations; currentThreadIndex += blockDim.x * gridDim.x) {
    int iFirstLine = currentThreadIndex / nLines;
    int iSecondLine = currentThreadIndex % nLines;
    // All unique pairs
    if (iSecondLine <= iFirstLine) {
      iFirstLine = nLines - iFirstLine - 2;
      iSecondLine = nLines - iSecondLine - 1;
    }
    if (Line::getDCA(lines[iFirstLine], lines[iSecondLine]) < pairCut) {
      ClusterLinesGPU cluster{lines[iFirstLine], lines[iSecondLine]};
      if (cluster.getVertex()[0] * cluster.getVertex()[0] + cluster.getVertex()[1] * cluster.getVertex()[1] < 1.98f * 1.98f) {
        // printOnThread(0, "xCentr: %f, yCentr: %f \n", cluster.getVertex()[0], cluster.getVertex()[1]);
        centroids[2 * currentThreadIndex] = cluster.getVertex()[0];
        centroids[2 * currentThreadIndex + 1] = cluster.getVertex()[1];
      } else {
        // write values outside the histogram boundaries,
        // default behaviour is not to have them added to histogram later
        // (writing zeroes would be problematic)
        centroids[2 * currentThreadIndex] = 2 * lowHistX;
        centroids[2 * currentThreadIndex + 1] = 2 * lowHistY;
      }
    } else {
      // write values outside the histogram boundaries,
      // default behaviour is not to have them added to histogram later
      // (writing zeroes would be problematic)
      centroids[2 * currentThreadIndex] = 2 * highHistX;
      centroids[2 * currentThreadIndex + 1] = 2 * highHistY;
    }
  }
}

GPUg() void computeZCentroidsKernel(
  const int nLines,
  const cub::KeyValuePair<int, int>* tmpVtX,
  float* beamPosition,
  Line* lines,
  float* centroids,
  const int* histX, // X
  const float lowHistX,
  const float binSizeHistX,
  const int nBinsHistX,
  const int* histY, // Y
  const float lowHistY,
  const float binSizeHistY,
  const int nBinsHistY,
  const float lowHistZ, // Z
  const float pairCut,
  const int binOpeningX,
  const int binOpeningY)
{
  for (unsigned int currentThreadIndex = blockIdx.x * blockDim.x + threadIdx.x; currentThreadIndex < nLines; currentThreadIndex += blockDim.x * gridDim.x) {
    if (tmpVtX[0].value || tmpVtX[1].value) {
      float tmpX{lowHistX + tmpVtX[0].key * binSizeHistX + binSizeHistX / 2};
      int sumWX{tmpVtX[0].value};
      float wX{tmpX * tmpVtX[0].value};
      for (int iBin{o2::gpu::GPUCommonMath::Max(0, tmpVtX[0].key - binOpeningX)}; iBin < o2::gpu::GPUCommonMath::Min(tmpVtX[0].key + binOpeningX + 1, nBinsHistX - 1); ++iBin) {
        if (iBin != tmpVtX[0].key) {
          wX += (lowHistX + iBin * binSizeHistX + binSizeHistX / 2) * histX[iBin];
          sumWX += histX[iBin];
        }
      }
      float tmpY{lowHistY + tmpVtX[1].key * binSizeHistY + binSizeHistY / 2};
      int sumWY{tmpVtX[1].value};
      float wY{tmpY * tmpVtX[1].value};
      for (int iBin{o2::gpu::GPUCommonMath::Max(0, tmpVtX[1].key - binOpeningY)}; iBin < o2::gpu::GPUCommonMath::Min(tmpVtX[1].key + binOpeningY + 1, nBinsHistY - 1); ++iBin) {
        if (iBin != tmpVtX[1].key) {
          wY += (lowHistY + iBin * binSizeHistY + binSizeHistY / 2) * histY[iBin];
          sumWY += histY[iBin];
        }
      }
      beamPosition[0] = wX / sumWX;
      beamPosition[1] = wY / sumWY;
      float mockBeamPoint1[3] = {beamPosition[0], beamPosition[1], -1}; // get two points laying at different z, to create line object
      float mockBeamPoint2[3] = {beamPosition[0], beamPosition[1], 1};
      Line pseudoBeam = {mockBeamPoint1, mockBeamPoint2};
      if (Line::getDCA(lines[currentThreadIndex], pseudoBeam) < pairCut) {
        ClusterLinesGPU cluster{lines[currentThreadIndex], pseudoBeam};
        centroids[currentThreadIndex] = cluster.getVertex()[2];
      } else {
        centroids[currentThreadIndex] = 2 * lowHistZ;
      }
    }
  }
}

GPUg() void computeVertexKernel(
  cub::KeyValuePair<int, int>* tmpVertexBins,
  int* histZ, // Z
  const float lowHistZ,
  const float binSizeHistZ,
  const int nBinsHistZ,
  Vertex* vertices,
  float* beamPosition,
  const int vertIndex,
  const int minContributors,
  const int binOpeningZ)
{
  for (unsigned int currentThreadIndex = blockIdx.x * blockDim.x + threadIdx.x; currentThreadIndex < binOpeningZ; currentThreadIndex += blockDim.x * gridDim.x) {
    if (currentThreadIndex == 0) {
      if (tmpVertexBins[2].value > 1 && (tmpVertexBins[0].value || tmpVertexBins[1].value)) {
        float z{lowHistZ + tmpVertexBins[2].key * binSizeHistZ + binSizeHistZ / 2};
        float ex{0.f};
        float ey{0.f};
        float ez{0.f};
        int sumWZ{tmpVertexBins[2].value};
        float wZ{z * tmpVertexBins[2].value};
        for (int iBin{o2::gpu::GPUCommonMath::Max(0, tmpVertexBins[2].key - binOpeningZ)}; iBin < o2::gpu::GPUCommonMath::Min(tmpVertexBins[2].key + binOpeningZ + 1, nBinsHistZ - 1); ++iBin) {
          if (iBin != tmpVertexBins[2].key) {
            wZ += (lowHistZ + iBin * binSizeHistZ + binSizeHistZ / 2) * histZ[iBin];
            sumWZ += histZ[iBin];
          }
          histZ[iBin] = 0;
        }
        if (sumWZ > minContributors || vertIndex == 0) {
          new (vertices + vertIndex) Vertex{o2::math_utils::Point3D<float>(beamPosition[0], beamPosition[1], wZ / sumWZ), o2::gpu::gpustd::array<float, 6>{ex, 0, ey, 0, 0, ez}, static_cast<ushort>(sumWZ), 0};
        } else {
          new (vertices + vertIndex) Vertex{};
        }
      } else {
        new (vertices + vertIndex) Vertex{};
      }
    }
  }
}
*/
} // namespace gpu
} // namespace its
} // namespace o2