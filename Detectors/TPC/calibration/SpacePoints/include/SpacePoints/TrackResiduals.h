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

/// \file TrackResiduals.h
/// \brief Definition of the TrackResiduals class
///
/// \author Ruben Shahoyan, ruben.shahoyan@cern.ch (original code in AliRoot)
///         Ole Schmidt, ole.schmidt@cern.ch (porting to O2)

#ifndef ALICEO2_TPC_TRACKRESIDUALS_H_
#define ALICEO2_TPC_TRACKRESIDUALS_H_

#include <memory>
#include <vector>
#include <array>
#include <bitset>
#include <string>

#include "DataFormatsTPC/Defs.h"
#include "SpacePoints/SpacePointsCalibParam.h"
#include "SpacePoints/SpacePointsCalibConfParam.h"
#include "SpacePoints/TrackInterpolation.h"

#include "TTree.h"
#include "TFile.h"

namespace o2
{
namespace tpc
{

/// \class TrackResiduals
/// This class is steering the space point calibration of the TPC from track residuals.
/// Residual maps are created using track interpolation from ITS/TRD/TOF tracks and comparing
/// them to the cluster positions in the TPC.
/// It has been ported from the AliTPCDcalibRes clas from AliRoot.
class TrackResiduals
{
 public:
  /// Default constructor
  TrackResiduals() = default;

  /// Copying and assigning is forbidden
  TrackResiduals(const TrackResiduals&) = delete;
  TrackResiduals& operator=(const TrackResiduals&) = delete;

  /// Enumeration for different voxel dimensions
  enum { VoxZ,          ///< Z/X index
         VoxF,          ///< Y/X index
         VoxX,          ///< X index
         VoxV,          ///< voxel dispersions
         VoxDim = 3,    ///< dimensionality of the voxels
         VoxHDim = 4 }; ///< dimensionality of the voxel + 1 for kernel weights

  /// Enumeration for the result indices
  enum { ResX,     ///< X index
         ResY,     ///< Y index
         ResZ,     ///< Z index
         ResD,     ///< index for dispersions
         ResDim }; ///< dimensionality for results structure (X, Y, Z and dispersions)

  /// Enumeration for voxel status flags, using the same bits as in AliRoot version
  enum { DistDone = 1 << 0,   ///< voxel residuals have been processed
         DispDone = 1 << 1,   ///< voxel dispersions have been processed
         SmoothDone = 1 << 2, ///< voxel has been smoothed
         Masked = 1 << 7 };   ///< voxel is masked

  enum class KernelType { Epanechnikov,
                          Gaussian };

  /// Structure which gets filled with the results for each voxel
  struct VoxRes {
    VoxRes() = default;
    std::array<float, ResDim> D{};            ///< values of extracted distortions
    std::array<float, ResDim> E{};            ///< their errors
    std::array<float, ResDim> DS{};           ///< smoothed residual
    std::array<float, ResDim> DC{};           ///< Cheb parameterized residual
    float EXYCorr{0.f};                       ///< correlation between extracted X and Y
    float dYSigMAD{0.f};                      ///< MAD estimator of dY sigma (dispersion after slope removal)
    float dZSigLTM{0.f};                      ///< Z sigma from unbinned LTM estimator
    std::array<float, VoxHDim> stat{};        ///< statistics: averages of each voxel dimension + entries
    std::array<unsigned char, VoxDim> bvox{}; ///< voxel identifier: VoxZ, VoxF, VoxX
    unsigned char bsec{0};                    ///< sector ID (0-35)
    unsigned char flags{0};                   ///< status flag
    ClassDefNV(VoxRes, 1);
  };

  /// Structure for local residuals (y/z position, dip angle, voxel identifier)
  /// after binning -> this is what will be written by the TPC residual aggregator device
  struct LocalResid {
    LocalResid() = default;
    LocalResid(short dyIn, short dzIn, short tgSlpIn, std::array<unsigned char, VoxDim> bvoxIn) : dy(dyIn), dz(dzIn), tgSlp(tgSlpIn), bvox(bvoxIn) {}
    short dy{0};                              ///< residual in y, ranges from -param::sMaxResid to +param::sMaxResid
    short dz{0};                              ///< residual in z, ranges from -param::sMaxResid to +param::sMaxResid
    short tgSlp{0};                           ///< tangens of the phi angle between padrow and track, ranges from -param::sMaxAngle to +param::sMaxAngle
    std::array<unsigned char, VoxDim> bvox{}; ///< voxel identifier: VoxZ, VoxF, VoxX
    ClassDefNV(LocalResid, 1);
  };

  /// Structure which holds the statistics for each voxel
  struct VoxStats {
    VoxStats() = default;
    std::array<float, VoxDim> meanPos{};
    float nEntries{0.f};
    ClassDefNV(VoxStats, 1);
  };

  // -------------------------------------- initialization --------------------------------------------------
  /// Steers the initialization (binning, default settings for smoothing, container for the results).
  /// \param initBinning Binning does not need to be initialized in case only outlier filtering is performed
  void init(bool doBinning = true);
  /// Initializes the binning in X, Y/X and Z.
  void initBinning();
  /// Initializes the results structure for given sector.
  /// For each voxel the bin indices are set and the COG is set to the center of the voxel.
  /// \param iSec TPC sector number
  void initResultsContainer(int iSec);
  /// Initialize the statistics for the local residuals container
  void initVoxelStats();
  /// Resets all (also intermediate) results
  void reset();

  // -------------------------------------- settings --------------------------------------------------
  /// Sets a flag to print the memory usage at certain points in the program for performance studies.
  void setPrintMemoryUsage() { mPrintMem = true; }
  /// Sets the kernel type used for smoothing.
  /// \param kernel Kernel type (Epanechnikov / Gaussian)
  /// \param bwX Bin width in X
  /// \param bwP Bin width in Y/X
  /// \param bwZ Bin width in Z
  /// \param scX Scale factor to increase smoothing bandwidth at sector edges in X
  /// \param scP Scale factor to increase smoothing bandwidth at sector edges in Y/X
  /// \param scZ Scale factor to increase smoothing bandwidth at sector edges in Z
  void setKernelType(KernelType kernel = KernelType::Epanechnikov, float bwX = 2.1f, float bwP = 2.1f, float bwZ = 1.7f, float scX = 1.f, float scP = 1.f, float scZ = 1.f);

  /// Setting the flag to true for a given dimension will enable smoothing with a 2nd order polynomial.
  /// Otherwise a first order polynomial will be used (default along z/x, since the bins are large)
  void setSmoothPol2(int dim, bool flag) { mSmoothPol2[dim] = flag; }

  void setVdriftCorr(float corr) { mEffVdriftCorr = corr; }

  void setT0Corr(float corr) { mEffT0Corr = corr; }

  // -------------------------------------- I/O --------------------------------------------------

  std::vector<LocalResid>& getLocalResVec() { return mLocalResidualsIn; }
  std::vector<VoxStats>** getVoxStatPtr() { return &mVoxStatsInPtr; }

  const std::array<std::vector<VoxRes>, SECTORSPERSIDE * SIDES>& getVoxelResults() const { return mVoxelResults; }

  // -------------------------------------- steering functions --------------------------------------------------

  /// Processes residuals for given sector.
  /// \param iSec Sector to process
  void processSectorResiduals(Int_t iSec);

  /// Performs the robust linear fit for one voxel to estimate the distortions in X, Y and Z and their errors.
  /// \param dy Vector with residuals in y
  /// \param dz Vector with residuals in z
  /// \param tg Vector with tan(phi) of the tracks
  /// \param resVox Voxel results structure
  void processVoxelResiduals(std::vector<float>& dy, std::vector<float>& dz, std::vector<float>& tg, VoxRes& resVox);

  /// Estimates dispersion for given voxel
  /// \param tg Vector with tan(phi) of the tracks
  /// \param dy Vector with residuals in y
  /// \param resVox Voxel results structure
  void processVoxelDispersions(std::vector<float>& tg, std::vector<float>& dy, VoxRes& resVox);

  /// Applies voxel validation cuts.
  /// Bad X bins are stored in mXBinsIgnore bitset
  /// \param iSec Sector to process
  /// \return Number of good rows in X
  int validateVoxels(int iSec);

  /// Smooths the residuals for given sector
  /// \param iSec Sector to process
  void smooth(int iSec);

  // -------------------------------------- statistics --------------------------------------------------

  /// Performs a robust linear fit y(x) = a + b * x for given x and y.
  /// The input data is trimmed to reject outliers.
  /// \param x First vector with input data
  /// \param y Second vector with input data
  /// \param res Array storing the fit results a and b
  /// \param err Array storing the uncertainties
  /// \param cutLTM Fraction of the input data to keep
  /// \return Median of the absolute deviations of the median of the data points to the fit
  float fitPoly1Robust(std::vector<float>& x, std::vector<float>& y, std::array<float, 2>& res, std::array<float, 3>& err, float cutLTM) const;

  /// Calculates the median of the absolute deviations to the median of the data.
  /// The input vector is copied such that the original vector is not modified.
  /// \param data Input data vector
  /// \return Median of absolute deviations to the median
  float getMAD2Sigma(std::vector<float> data) const;

  /// Fits a straight line to given x and y minimizing the absolute deviations y(x|a, b) = a + b * x.
  /// Not all data points need to be considered, but only a fraction of the input is used to perform the fit.
  /// \param nPoints Number of points to consider
  /// \param offset Starting index for the input vectors
  /// \param x First vector with input data
  /// \param y Second vector with input data
  /// \param a Stores the result for a
  /// \param b Stores the result for b
  /// \param err Stores the uncertainties
  void medFit(int nPoints, int offset, const std::vector<float>& x, const std::vector<float>& y, float& a, float& b, std::array<float, 3>& err) const;

  /// Helper function for medFit.
  /// Calculates sum(x_i * sgn(y_i - a - b * x_i)) for a given b
  /// \param nPoints Number of points to consider
  /// \param offset Starting index for the input vectors
  /// \param x First vector with input data
  /// \param y Second vector with input data
  /// \param b Given b
  /// \param aa Parameter a for linear fit (will be set by roFunc)
  /// \return The calculated sum
  float roFunc(int nPoints, int offset, const std::vector<float>& x, const std::vector<float>& y, float b, float& aa) const;

  /// Returns the k-th smallest value in the vector.
  /// The input vector is rearranged such that the k-th smallest value is at the k-th position.
  /// \todo Can probably be replaced by std::nth_element(), need to check which one is faster
  /// All smaller values will be placed in before it in arbitrary order, all large values behind it in arbitrary order.
  /// \param k Which value to get
  /// \param data Vector with input data
  /// \return k-th smallest value in the input vector
  float selectKthMin(const int k, std::vector<float>& data) const;

  /// Calculates a smooth estimate for the distortions in specified dimensions around the COG for a given voxel.
  /// \param iSec Sector in which the voxel is located
  /// \param x COG position in X
  /// \param p COG position in Y/X
  /// \param z COG position in Z
  /// \param res Array to store the results
  /// \param whichDim Integer value with bits set for the dimensions which need to be smoothed
  /// \return Flag if the estimate was successfull
  bool getSmoothEstimate(int iSec, float x, float p, float z, std::array<float, ResDim>& res, int whichDim = 0);

  /// Calculates the weight of the given point used for the kernel smoothing.
  /// Takes into account the defined kernel in mKernelType.
  /// \param u2vec Weighted distance in X, Y/X and Z
  /// \return Kernel weight
  double getKernelWeight(std::array<double, 3> u2vec) const;

  /// Fits a circle to a given set of points in x and y. Kasa algorithm is used.
  /// \param nCl number of used points
  /// \param x array with values for x
  /// \param y array with values for y
  /// \param xc fit result for circle center position in x is stored here
  /// \param yc fit result for circle center position in y is stored here
  /// \param r fit result for circle radius is stored here
  /// \param residHelixY residuals in y from fitted circle to given points is stored here
  static void fitCircle(int nCl, std::array<float, param::NPadRows>& x, std::array<float, param::NPadRows>& y, float& xc, float& yc, float& r, std::array<float, param::NPadRows>& residHelixY);

  /// Fits a straight line to a given set of points, w/o taking into account measurement errors or different weights for the points
  /// Straight line is given by y = a * x + b
  /// \param res[0] contains the slope (a)
  /// \param res[1] contains the offset (b)
  static bool fitPoly1(int nCl, std::array<float, param::NPadRows>& x, std::array<float, param::NPadRows>& y, std::array<float, 2>& res);

  // -------------------------------------- binning / geometry --------------------------------------------------

  /// Sets the number of bins used in x direction
  /// \param nBins number of bins
  void setNXBins(int nBins) { mNXBins = nBins; }
  int getNXBins() const { return mNXBins; }

  /// Sets the number of bins used in y/x direction
  /// \param nBins number of bins
  void setNY2XBins(int nBins) { mNY2XBins = nBins; }
  int getNY2XBins() const { return mNY2XBins; }

  /// Sets the number of bins used in z/x direction
  /// \param nBins number of bins
  void setNZ2XBins(int nBins) { mNZ2XBins = nBins; }
  int getNZ2XBins() const { return mNZ2XBins; }

  /// Get the total number of voxels per TPC sector (mNXBins * mNY2XBins * mNZ2XBins)
  int getNVoxelsPerSector() const { return mNVoxPerSector; }

  /// Sets a custom (non-uniform) binning in y/x
  /// \param binning Vector with custom binning from -1 to 1
  void setY2XBinning(const std::vector<float>& binning);

  /// Sets a custom (non-uniform) binning in z/x
  /// \param binning Vector with custom binning from 0 to 1
  void setZ2XBinning(const std::vector<float>& binning);

  /// Calculates the global bin number
  /// \param ix Bin index in X
  /// \param ip Bin index in Y/X
  /// \param iz Bin index in Z/X
  /// \return global bin number
  size_t getGlbVoxBin(int ix, int ip, int iz) const;

  /// Calculates the global bin number
  /// \param bvox Array with the voxels bin indices in X, Y/X and Z/X
  /// \return global bin number
  size_t getGlbVoxBin(const std::array<unsigned char, VoxDim>& bvox) const;

  /// Calculates the coordinates of the center for a given voxel.
  /// These are not global TPC coordinates, but the coordinates for the given global binning system.
  /// \param isec The sector in which we are
  /// \param ix Bin index in X
  /// \param ip Bin index in Y/X
  /// \param iz Bin index in Z/X
  /// \param x Coordinate in X
  /// \param p Coordinate in Y/X
  /// \param z Coordinate in Z/X
  void getVoxelCoordinates(int isec, int ix, int ip, int iz, float& x, float& p, float& z) const;

  /// Calculates the x-coordinate for given x bin.
  /// \param i Bin index
  /// \return Coordinate in X
  float getX(int i) const;

  /// Calculates the y/x-coordinate.
  /// \param ix Bin index in X
  /// \param ip Bin index in Y/X
  /// \return Coordinate in Y/X
  float getY2X(int ix, int ip) const;

  /// Calculates the z-coordinate for given z bin
  /// \param i Bin index in Z/X
  /// \return Coordinate in Z/X
  float getZ2X(int iz) const;

  /// Tests whether a bin in X is set to be ignored.
  /// \param iSec Sector number
  /// \param bin Bin index in X
  /// \return Ignore flag
  bool getXBinIgnored(int iSec, int bin) const { return mXBinsIgnore[iSec].test(bin); }

  /// Calculates the bin indices of the closest voxel.
  /// \param x Coordinate in X
  /// \param y2x Coordinate in Y/X
  /// \param z2x Coordinate in Z
  /// \param ix Resulting bin index in X
  /// \param ip Resulting bin index in Y/X
  /// \param iz Resulting bin index in Z/X
  void findVoxel(float x, float y2x, float z2x, int& ix, int& ip, int& iz) const;

  /// Calculates the bin indices for given x, y, z in sector coordinates
  bool findVoxelBin(int secID, float x, float y, float z, std::array<unsigned char, VoxDim>& bvox) const;

  /// Transforms X coordinate to bin index
  /// \param x Coordinate in X
  /// \return Bin index in X
  int getXBin(float x) const;

  /// Transforms X coordinate to bin index
  /// \param x Coordinate in X
  /// \return Bin index in X if x is in valid range, -1 otherwise
  int getXBinExact(float x) const;

  /// Get pad-row number for given x
  /// \param x Coordinate in X
  /// \return Pad row number if x is in valid range, -1 otherwise
  int getRowID(float x) const;

  /// Transforms Y/X coordinate to bin index at given X
  /// In case y2x is out of range -1 or mNY2XBins is returned resp.
  /// \param y2x Coordinate in Y/X
  /// \param ix Bin index in X
  /// \return Bin index in Y/X
  int getY2XBinExact(float y2x, int ix) const;

  /// Transforms Y/X coordinate to bin index at given X
  /// In case y2x is out of range the first or last bin is returned resp.
  /// \param y2x Coordinate in Y/X
  /// \param ix Bin index in X
  /// \return Bin index in Y/X
  int getY2XBin(float y2x, int ix) const;

  /// Transforms Z/X coordinate to bin index
  /// In case z2x is out of range XX or XXX is returned resp.
  /// \param z2x Coordinate in Z/X
  /// \return Bin index in Z/X
  int getZ2XBinExact(float z2x) const;

  /// Transforms Z/X coordinate to bin index
  /// In case z2x is out of range the first or last bin is returned resp.
  /// \param z2x Coordinate in Z/X
  /// \return Bin index in Z/X
  int getZ2XBin(float z2x) const;

  /// Returns the inverse of the distance between two bins in X
  /// \param ix Bin index in X
  /// \return Inverse of the distance between bins
  float getDXI(int ix) const;

  /// Returns the inverse of the distance between two bins in Y/X
  /// \param ix Bin index in X
  /// \param iy Bin index in Y/X (needed for non-uniform binning)
  /// \return Inverse of the distance between bins
  float getDY2XI(int ix, int iy = 0) const;

  /// Returns the bin size in Z/X
  /// \param iz Bin index in Z/X
  /// \return Bin size
  float getDZ2X(int iz = 0) const;

  /// Returns the inverse of the distance between two bins in Z/X
  /// \param iz Bin index in Z/X (needed for non-uniform binning)
  /// \return Inverse of the distance between bins
  float getDZ2XI(int iz = 0) const;

  // -------------------------------------- debugging --------------------------------------------------

  /// Prints the current memory usage
  void printMem() const;

  /// Dumps the full results for a given sector to the debug tree (only if an output file has been created before).
  /// \param iSec Sector to dump
  void dumpResults(int iSec);

  /// Creates a file for the debug output.
  void createOutputFile(const char* filename = "debugVoxRes.root");

  /// Closes the file with the debug output.
  void closeOutputFile();

  /// Allow to access the output file from outside
  TFile* getOutputFilePtr() { return mFileOut.get(); }

  /// Set the voxel statistics directly from outside
  void setStats(const std::vector<TrackResiduals::VoxStats>& statsIn, int iSec);

  /// Fill statistics from TTree
  void fillStats(int iSec);

  /// clear member to be able to process new sector or new input files
  void clear();

  /// output tree
  TTree* getOutputTree() { return mTreeOut.get(); }

 private:
  std::bitset<SECTORSPERSIDE * SIDES> mInitResultsContainer{};

  // some constants
  static constexpr float sFloatEps{1.e-7f}; ///< float epsilon for robust linear fitting
  static constexpr float sDeadZone{1.5f};   ///< dead zone for TPC in between sectors
  static constexpr int sSmtLinDim{4};       ///< max matrix size for smoothing (pol1)
  static constexpr int sMaxSmtDim{7};       ///< max matrix size for smoothing (pol2)

  // settings
  const SpacePointsCalibConfParam* mParams = nullptr;

  // input data
  std::vector<LocalResid> mLocalResidualsIn;                        ///< binned local residuals from aggregator
  std::vector<VoxStats> mVoxStatsIn, *mVoxStatsInPtr{&mVoxStatsIn}; ///< the statistics information for each voxel from the aggregator
  // output data
  std::unique_ptr<TFile> mFileOut; ///< output debug file
  std::unique_ptr<TTree> mTreeOut; ///< tree holding debug output
  // status flags
  bool mIsInitialized{}; ///< initialize only once
  bool mPrintMem{};      ///< turn on to print memory usage at certain points
  // binning
  int mNXBins{param::NPadRows};                            ///< number of bins in radial direction
  int mNY2XBins{param::NY2XBins};                          ///< number of y/x bins per sector
  int mNZ2XBins{param::NZ2XBins};                          ///< number of z/x bins per sector
  int mNVoxPerSector{};                                    ///< number of voxels per sector
  float mDX{};                                             ///< x bin size
  float mDXI{};                                            ///< inverse of x bin size
  std::vector<float> mMaxY2X{};                            ///< max y/x at each x bin, accounting dead zones
  std::vector<float> mDY2X{};                              ///< y/x bin size at given x bin
  std::vector<float> mDY2XI{};                             ///< inverse y/x bin size at given x bin
  std::vector<float> mY2XBinsDH{};                         ///< half width in y/x within the interval [-1..1]
  std::vector<float> mY2XBinsDI{};                         ///< inverse bin width in y/x within the interval [-1..1]
  std::vector<float> mY2XBinsCenter{};                     ///< bin center in y/x within the interval [-1..1]
  float mDZ2X{};                                           ///< bin size in z/x
  float mDZ2XI{};                                          ///< inverse of bin size in z/x
  std::vector<float> mZ2XBinsDH{};                         ///< half width in z/x within the interval [0..1]
  std::vector<float> mZ2XBinsDI{};                         ///< inverse bin width in z/x within the interval [0..1]
  std::vector<float> mZ2XBinsCenter{};                     ///< bin center in z/x within the interval [0..1]
  float mMaxZ2X{1.f};                                      ///< max z/x value
  std::array<bool, VoxDim> mUniformBins{true, true, true}; ///< if binning is uniform for each dimension
  // smoothing
  KernelType mKernelType{KernelType::Epanechnikov};                ///< kernel type (Epanechnikov / Gaussian)
  bool mUseErrInSmoothing{true};                                   ///< weight kernel by point error
  std::array<bool, VoxDim> mSmoothPol2{};                          ///< option to use pol1 or pol2 in each direction
  std::array<int, SECTORSPERSIDE * SIDES> mNSmoothingFailedBins{}; ///< number of failed bins / sector
  std::array<int, VoxDim> mStepKern{};                             ///< N bins to consider with given kernel settings
  std::array<float, VoxDim> mKernelScaleEdge{};                    ///< optional scaling factors for kernel width on the edge
  std::array<float, VoxDim> mKernelWInv{};                         ///< inverse kernel width in bins
  std::array<double, ResDim * sMaxSmtDim> mLastSmoothingRes{};     ///< results of last smoothing operation
  // calibrated parameters
  float mEffVdriftCorr{0.f}; ///< global correction factor for vDrift based on d(delta(z))/dz fit
  float mEffT0Corr{0.f};     ///< global correction for T0 shift from offset of d(delta(z))/dz fit
  // (intermediate) results
  std::array<std::bitset<param::NPadRows>, SECTORSPERSIDE * SIDES> mXBinsIgnore{};          ///<! flags which X bins to ignore
  std::array<std::array<float, param::NPadRows>, SECTORSPERSIDE * SIDES> mValidFracXBins{}; ///< for each sector for each X-bin the fraction of validated voxels
  std::array<std::vector<VoxRes>, SECTORSPERSIDE * SIDES> mVoxelResults{};                  ///< results per sector and per voxel for 3-D distortions
  VoxRes mVoxelResultsOut{};                                                                ///< the results from mVoxelResults are copied in here to be able to stream them
  VoxRes* mVoxelResultsOutPtr{&mVoxelResultsOut};                                           ///< pointer to set the branch address to for the output

  ClassDefNV(TrackResiduals, 3);
};

//_____________________________________________________
inline size_t TrackResiduals::getGlbVoxBin(const std::array<unsigned char, VoxDim>& bvox) const
{
  return bvox[VoxX] + (bvox[VoxF] + bvox[VoxZ] * mNY2XBins) * mNXBins;
}

//_____________________________________________________
inline size_t TrackResiduals::getGlbVoxBin(int ix, int ip, int iz) const
{
  return ix + (ip + iz * mNY2XBins) * mNXBins;
}

//_____________________________________________________
inline void TrackResiduals::getVoxelCoordinates(int isec, int ix, int ip, int iz, float& x, float& p, float& z) const
{
  x = getX(ix);
  p = getY2X(ix, ip);
  z = getZ2X(iz);
  if (isec >= SECTORSPERSIDE) {
    z = -z;
  }
}

//_____________________________________________________
inline float TrackResiduals::getDXI(int ix) const
{
  if (mUniformBins[VoxX]) {
    return mDXI;
  } else {
    if (ix < param::NRowsPerROC[0]) {
      // we are in the IROC
      return 1.f / param::RowDX[0];
    } else if (ix > param::NRowsAccumulated[param::NROCTypes - 2]) {
      // we are in the last OROC
      return 1.f / param::RowDX[param::NROCTypes - 1];
    } else if (ix < param::NRowsAccumulated[1]) {
      // OROC1
      return 1.f / param::RowDX[1];
    } else {
      // OROC2
      return 1.f / param::RowDX[2];
    }
  }
}

//_____________________________________________________
inline float TrackResiduals::getX(int i) const
{
  return mUniformBins[VoxX] ? param::MinX + (i + 0.5) * mDX : param::RowX[i];
}

//_____________________________________________________
inline float TrackResiduals::getY2X(int ix, int ip) const
{
  if (mUniformBins[VoxF]) {
    return (0.5f + ip) * mDY2X[ix] - mMaxY2X[ix];
  }
  return mMaxY2X[ix] * mY2XBinsCenter[ip];
}

//_____________________________________________________
inline float TrackResiduals::getZ2X(int iz) const
{
  if (mUniformBins[VoxZ]) {
    return (0.5f + iz) * getDZ2X();
  }
  return mZ2XBinsCenter[iz];
}

//_____________________________________________________
inline float TrackResiduals::getDY2XI(int ix, int iy) const
{
  if (mUniformBins[VoxF]) {
    return mDY2XI[ix];
  }
  return mY2XBinsDI[iy] / mMaxY2X[ix];
}

//_____________________________________________________
inline float TrackResiduals::getDZ2X(int iz) const
{
  if (mUniformBins[VoxZ]) {
    return mDZ2X;
  }
  return 2.f * mZ2XBinsDH[iz];
}

//_____________________________________________________
inline float TrackResiduals::getDZ2XI(int iz) const
{
  if (mUniformBins[VoxZ]) {
    return mDZ2XI;
  }
  return mZ2XBinsDI[iz];
}

//_____________________________________________________
inline void TrackResiduals::findVoxel(float x, float y2x, float z2x, int& ix, int& ip, int& iz) const
{
  ix = getXBin(x);
  ip = getY2XBin(y2x, ix);
  iz = getZ2XBin(z2x);
}

//_____________________________________________________
inline int TrackResiduals::getXBinExact(float x) const
{
  if (mUniformBins[VoxX]) {
    int ix = (x - param::MinX) * mDXI;
    return (ix < 0 || ix >= mNXBins) ? -1 : ix;
  }
  return getRowID(x);
}

//_____________________________________________________
inline int TrackResiduals::getXBin(float x) const
{
  int bx = getXBinExact(x);
  return bx > -1 ? (bx < mNXBins ? bx : mNXBins - 1) : 0;
}

//_____________________________________________________
inline int TrackResiduals::getY2XBinExact(float y2x, int ix) const
{
  if (y2x < -mMaxY2X[ix]) {
    return -1;
  }
  if (y2x > mMaxY2X[ix]) {
    return mNY2XBins;
  }
  if (mUniformBins[VoxF]) {
    return static_cast<int>((y2x + mMaxY2X[ix]) * getDY2XI(ix));
  }
  y2x /= mMaxY2X[ix];
  for (int iBin = 0; iBin < mNY2XBins; ++iBin) {
    if (y2x < mY2XBinsCenter[iBin] + mY2XBinsDH[iBin]) {
      return iBin;
    }
  }
  return mNY2XBins;
}

//_____________________________________________________
inline int TrackResiduals::getY2XBin(float y2x, int ix) const
{
  int bp = getY2XBinExact(y2x, ix);
  return bp > -1 ? (bp < mNY2XBins ? bp : mNY2XBins - 1) : 0;
}

//_____________________________________________________
inline int TrackResiduals::getZ2XBinExact(float z2x) const
{
  if (mUniformBins[VoxZ]) {
    float bz = z2x * getDZ2XI();
    if (bz >= mNZ2XBins) {
      return -1;
    }
    if (bz < 0) {
      // accounting for clusters which were moved to the wrong side
      // TODO: how can this happen?
      bz = 0;
    }
    return static_cast<int>(bz);
  }
  for (int iBin = 0; iBin < mNZ2XBins; ++iBin) {
    if (z2x < mZ2XBinsCenter[iBin] + mZ2XBinsDH[iBin]) {
      return iBin;
    }
  }
  return -1;
}

//_____________________________________________________
inline int TrackResiduals::getZ2XBin(float z2x) const
{
  int iz = getZ2XBinExact(z2x);
  return iz < 0 ? mNZ2XBins - 1 : iz;
}

} // namespace tpc

} // namespace o2

#endif
