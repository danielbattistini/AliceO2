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

/// \file GPUO2Interface.h
/// \author David Rohr

#ifndef GPUO2INTERFACEUTILS_H
#define GPUO2INTERFACEUTILS_H

#include <functional>
#include <memory>

namespace o2
{
struct InteractionRecord;
namespace raw
{
class RawFileWriter;
} // namespace raw
namespace tpc
{
class CalibdEdxContainer;
class Digit;
template <class T>
class CalDet;
} // namespace tpc
} // namespace o2

namespace o2::gpu
{
struct GPUParam;
struct GPUO2InterfaceConfiguration;
struct GPUSettingsO2;
struct TPCPadGainCalib;
class GPUO2InterfaceUtils
{
 public:
  static std::unique_ptr<TPCPadGainCalib> getPadGainCalibDefault();
  static std::unique_ptr<TPCPadGainCalib> getPadGainCalib(const o2::tpc::CalDet<float>& in);
  static std::unique_ptr<o2::tpc::CalibdEdxContainer> getCalibdEdxContainerDefault();
  template <class S>
  static void RunZSEncoder(const S& in, std::unique_ptr<uint64_t[]>* outBuffer, uint32_t* outSizes, o2::raw::RawFileWriter* raw, const o2::InteractionRecord* ir, int32_t version, bool verify, float threshold = 0.f, bool padding = false, std::function<void(std::vector<o2::tpc::Digit>&)> digitsFilter = nullptr);
  template <class S>
  static void RunZSEncoder(const S& in, std::unique_ptr<uint64_t[]>* outBuffer, uint32_t* outSizes, o2::raw::RawFileWriter* raw, const o2::InteractionRecord* ir, GPUO2InterfaceConfiguration& config, int32_t version, bool verify, bool padding = false, std::function<void(std::vector<o2::tpc::Digit>&)> digitsFilter = nullptr);
  template <class T>
  static float getNominalGPUBz(T& src)
  {
    return (5.00668f / 30000.f) * src.getL3Current();
  }
  static std::unique_ptr<GPUParam> getFullParam(float solenoidBz, uint32_t nHbfPerTf = 0, std::unique_ptr<GPUO2InterfaceConfiguration>* pConfiguration = nullptr, std::unique_ptr<GPUSettingsO2>* pO2Settings = nullptr, bool* autoMaxTimeBin = nullptr);
  static std::shared_ptr<GPUParam> getFullParamShared(float solenoidBz, uint32_t nHbfPerTf = 0, std::unique_ptr<GPUO2InterfaceConfiguration>* pConfiguration = nullptr, std::unique_ptr<GPUSettingsO2>* pO2Settings = nullptr, bool* autoMaxTimeBin = nullptr); // Return owning pointer
  static void paramUseExternalOccupancyMap(GPUParam* param, uint32_t nHbfPerTf, const uint32_t* occupancymap, int32_t occupancyMapSize);
  static uint32_t getTpcMaxTimeBinFromNHbf(uint32_t nHbf);

  class GPUReconstructionZSDecoder
  {
   public:
    void DecodePage(std::vector<o2::tpc::Digit>& outputBuffer, const void* page, uint32_t tfFirstOrbit, const GPUParam* param, uint32_t triggerBC = 0);

   private:
    std::vector<std::function<void(std::vector<o2::tpc::Digit>&, const void*, uint32_t, uint32_t)>> mDecoders;
  };
};

} // namespace o2::gpu

#endif
