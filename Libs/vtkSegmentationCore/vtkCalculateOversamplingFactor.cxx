/*==============================================================================

  Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
  Queen's University, Kingston, ON, Canada. All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file was originally developed by Csaba Pinter, PerkLab, Queen's University
  and was supported through the Applied Cancer Research Unit program of Cancer Care
  Ontario with funds provided by the Ontario Ministry of Health and Long-Term Care

==============================================================================*/

// SegmentationCore includes
#include "vtkCalculateOversamplingFactor.h"

// VTK includes
#include <vtkCollection.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkObjectFactory.h>
#include <vtkTimerLog.h>
#include <vtkPolyData.h>
#include <vtkPiecewiseFunction.h>
#include <vtkSmartPointer.h>
#include <vtkVersion.h>

// STD includes
#include <math.h>
#include <algorithm>

//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkCalculateOversamplingFactor);

//----------------------------------------------------------------------------
vtkCalculateOversamplingFactor::vtkCalculateOversamplingFactor()
{
  this->InputPolyData = NULL;
  this->ReferenceGeometryImageData = NULL;
  this->OutputOversamplingFactor = 1;
  this->MassPropertiesAlgorithm = NULL;
  this->LogSpeedMeasurementsOff();
}

//----------------------------------------------------------------------------
vtkCalculateOversamplingFactor::~vtkCalculateOversamplingFactor()
{
  this->SetInputPolyData(NULL);
  this->SetReferenceGeometryImageData(NULL);
  this->SetMassPropertiesAlgorithm(NULL);
}

//----------------------------------------------------------------------------
void vtkCalculateOversamplingFactor::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
bool vtkCalculateOversamplingFactor::CalculateOversamplingFactor()
{
  // Set a safe value to use even if the return value is not checked
  this->OutputOversamplingFactor = 1;

  if (!this->InputPolyData)
    {
    vtkErrorMacro("CalculateOversamplingFactor: Invalid input poly data!");
    return false;
    }
  if (!this->ReferenceGeometryImageData)
    {
    vtkErrorMacro("CalculateOversamplingFactor: Invalid rasterization reference volume node!");
    return false;
    }

  // Mark start time
  vtkSmartPointer<vtkTimerLog> timer = vtkSmartPointer<vtkTimerLog>::New();
#ifndef NDEBUG
  double checkpointStart = timer->GetUniversalTime();
#endif

  // Create mass properties algorithm for common use
  vtkSmartPointer<vtkMassProperties> massProperties = vtkSmartPointer<vtkMassProperties>::New();
  this->SetMassPropertiesAlgorithm(massProperties);
  massProperties->SetInputData(this->InputPolyData);
  // Run algorithm so that results can be extracted for relative structure size calculation and complexity measure
  massProperties->Update();

  // Get relative structure size
  double relativeStructureSize = this->CalculateRelativeStructureSize();
  if (relativeStructureSize == -1.0)
    {
    vtkErrorMacro("CalculateOversamplingFactor: Failed to calculate relative structure size");
    return false;
    }

  // Get complexity measure
  double complexityMeasure = this->CalculateComplexityMeasure();
  if (complexityMeasure == -1.0)
    {
    vtkErrorMacro("CalculateOversamplingFactor: Failed to calculate complexity measure");
    return false;
    }
#ifndef NDEBUG
  double checkpointFuzzyStart = timer->GetUniversalTime();
#endif

  // Determine crisp oversampling factor based on crisp inputs using fuzzy rules
  this->OutputOversamplingFactor = this->DetermineOversamplingFactor(relativeStructureSize, complexityMeasure);

  vtkDebugMacro("CalculateOversamplingFactor: Automatic oversampling factor of " << this->OutputOversamplingFactor << " has been calculated.");

  if (this->LogSpeedMeasurements)
    {
#ifndef NDEBUG
    double checkpointEnd = timer->GetUniversalTime();
#endif
    vtkDebugMacro("CalculateOversamplingFactor: Total automatic oversampling calculation time: " << checkpointEnd-checkpointStart << " s\n"
      << "\tCalculating relative structure size and complexity measure: " << checkpointFuzzyStart-checkpointStart << " s\n"
      << "\tDetermining oversampling factor using fuzzy rules: " << checkpointEnd-checkpointFuzzyStart << " s");
    }

  // Clean up (triggers destruction of member)
  this->SetMassPropertiesAlgorithm(NULL);

  return true;
}

//----------------------------------------------------------------------------
double vtkCalculateOversamplingFactor::CalculateRelativeStructureSize()
{
  if (!this->InputPolyData)
    {
    vtkErrorMacro("CalculateRelativeStructureSize: Invalid input poly data!");
    return -1.0;
    }
  if (!this->ReferenceGeometryImageData)
    {
    vtkErrorMacro("CalculateRelativeStructureSize: Invalid rasterization reference volume node!");
    return -1.0;
    }
  if (!this->MassPropertiesAlgorithm)
    {
    vtkErrorMacro("CalculateRelativeStructureSize: Invalid mass properties algorithm!");
    return -1.0;
    }

  // Get structure volume in mm^3
  double structureVolume = this->MassPropertiesAlgorithm->GetVolume();

  // Sanity check
  double structureProjectedVolume = this->MassPropertiesAlgorithm->GetVolumeProjected();
  double error = (structureVolume - structureProjectedVolume);
  if (error * 10000 > structureVolume)
    {
    vtkDebugMacro("CalculateRelativeStructureSize: Computed structure volume may be invalid according to difference in calculated projected and normal volumes.");
    }

  // Calculate reference volume in mm^3
  int dimensions[3] = {0,0,0};
  this->ReferenceGeometryImageData->GetDimensions(dimensions);
  double spacing[3] = {0.0,0.0,0.0};
  this->ReferenceGeometryImageData->GetSpacing(spacing);
  double volumeVolume = dimensions[0]*dimensions[1]*dimensions[2] * spacing[0]*spacing[1]*spacing[2]; // Number of voxels * volume of one voxel

  double relativeStructureSize = structureVolume / volumeVolume;

  // Map raw measurement to the fuzzy input scale
  double sizeMeasure = (-1.0) * log10(relativeStructureSize);
  vtkDebugMacro("CalculateRelativeStructureSize: Relative structure size: " << relativeStructureSize << ", size measure: " << sizeMeasure);

  return sizeMeasure;
}

//----------------------------------------------------------------------------
double vtkCalculateOversamplingFactor::CalculateComplexityMeasure()
{
  if (!this->InputPolyData)
    {
    vtkErrorMacro("CalculateComplexityMeasure: Invalid input poly data!");
    return -1.0;
    }
  if (!this->MassPropertiesAlgorithm)
    {
    vtkErrorMacro("CalculateComplexityMeasure: Invalid mass properties algorithm!");
    return -1.0;
    }

  // Normalized shape index (NSI) characterizes the deviation of the shape of an object
  // from a sphere (from surface area and volume). A sphere's NSI is one. This number is always >= 1.0
  double normalizedShapeIndex = this->MassPropertiesAlgorithm->GetNormalizedShapeIndex();

  // Map raw measurement to the fuzzy input scale
  double complexityMeasure = std::max(normalizedShapeIndex - 1.0, 0.0); // If smaller then 0, then return 0
  vtkDebugMacro("CalculateComplexityMeasure: Normalized shape index: " << normalizedShapeIndex << ", complexity measure: " << complexityMeasure);

  return complexityMeasure;
}

//---------------------------------------------------------------------------
// Fuzzy membership functions:
// https://www.assembla.com/spaces/slicerrt/documents/bzADACUi8r5kGMdmr6bg7m/download/bzADACUi8r5kGMdmr6bg7m
//
// Fuzzy rules:
// 1. If RSS is Very small, then Oversampling is Very high
// 2. If RSS is Small and Complexity is High then Oversampling is High
// 3. If RSS is Medium and Complexity is High then Oversampling is High
// 4. If RSS is Small and Complexity is Low then Oversampling is Normal
// 5. If RSS is Medium and Complexity is Low then Oversampling is Normal
// 6. If RSS is Large, then Oversampling is Low
//---------------------------------------------------------------------------
double vtkCalculateOversamplingFactor::DetermineOversamplingFactor(double relativeStructureSize, double complexityMeasure)
{
  if (relativeStructureSize == -1.0 || complexityMeasure == -1.0)
    {
    vtkErrorMacro("DetermineOversamplingFactor: Invalid input measures! Returning default oversampling of 1");
    return 1.0;
    }

  // Define input membership functions for relative structure size
  vtkSmartPointer<vtkPiecewiseFunction> sizeLarge = vtkSmartPointer<vtkPiecewiseFunction>::New();
  sizeLarge->AddPoint(0.5, 1);
  sizeLarge->AddPoint(2, 0);
  vtkSmartPointer<vtkPiecewiseFunction> sizeMedium = vtkSmartPointer<vtkPiecewiseFunction>::New();
  sizeMedium->AddPoint(0.5, 0);
  sizeMedium->AddPoint(2, 1);
  sizeMedium->AddPoint(2.5, 1);
  sizeMedium->AddPoint(3, 0);
  vtkSmartPointer<vtkPiecewiseFunction> sizeSmall = vtkSmartPointer<vtkPiecewiseFunction>::New();
  sizeSmall->AddPoint(2.5, 0);
  sizeSmall->AddPoint(3, 1);
  sizeSmall->AddPoint(3.25, 1);
  sizeSmall->AddPoint(3.75, 0);
  vtkSmartPointer<vtkPiecewiseFunction> sizeVerySmall = vtkSmartPointer<vtkPiecewiseFunction>::New();
  sizeVerySmall->AddPoint(3.25, 0);
  sizeVerySmall->AddPoint(3.75, 1);

  // Define input membership functions for complexity measure
  vtkSmartPointer<vtkPiecewiseFunction> complexityLow = vtkSmartPointer<vtkPiecewiseFunction>::New();
  complexityLow->AddPoint(0.2, 1);
  complexityLow->AddPoint(0.6, 0);
  vtkSmartPointer<vtkPiecewiseFunction> complexityHigh = vtkSmartPointer<vtkPiecewiseFunction>::New();
  complexityHigh->AddPoint(0.2, 0);
  complexityHigh->AddPoint(0.6, 1);

  // Define output membership functions for oversampling power
  // (the output oversampling factor will be 2 to the power of this number)
  vtkSmartPointer<vtkPiecewiseFunction> oversamplingLow = vtkSmartPointer<vtkPiecewiseFunction>::New();
  oversamplingLow->AddPoint(-1.25, 1);
  oversamplingLow->AddPoint(-0.75, 1);
  oversamplingLow->AddPoint(0.25, 0);
  vtkSmartPointer<vtkPiecewiseFunction> oversamplingNormal = vtkSmartPointer<vtkPiecewiseFunction>::New();
  oversamplingNormal->AddPoint(-0.75, 0);
  oversamplingNormal->AddPoint(0.25, 1);
  oversamplingNormal->AddPoint(0.25, 1);
  oversamplingNormal->AddPoint(0.75, 0);
  vtkSmartPointer<vtkPiecewiseFunction> oversamplingHigh = vtkSmartPointer<vtkPiecewiseFunction>::New();
  oversamplingHigh->AddPoint(0.25, 0);
  oversamplingHigh->AddPoint(0.75, 1);
  oversamplingHigh->AddPoint(1.25, 1);
  oversamplingHigh->AddPoint(1.75, 0);
  vtkSmartPointer<vtkPiecewiseFunction> oversamplingVeryHigh = vtkSmartPointer<vtkPiecewiseFunction>::New();
  oversamplingVeryHigh->AddPoint(1.25, 0);
  oversamplingVeryHigh->AddPoint(1.75, 1);
  oversamplingVeryHigh->AddPoint(2.25, 1);

  // Fuzzify inputs
  double sizeLargeMembership = sizeLarge->GetValue(relativeStructureSize);
  double sizeMediumMembership = sizeMedium->GetValue(relativeStructureSize);
  double sizeSmallMembership = sizeSmall->GetValue(relativeStructureSize);
  double sizeVerySmallMembership = sizeVerySmall->GetValue(relativeStructureSize);

  double complexityLowMembership = complexityLow->GetValue(complexityMeasure);
  double complexityHighMembership = complexityHigh->GetValue(complexityMeasure);

  // Apply rules and determine consequents

  // 1. If RSS is Very small, then Oversampling is Very high
  double rule1_OversamplingVeryHighClippingValue = sizeVerySmallMembership;
  // 2. If RSS is Small and Complexity is High then Oversampling is High
  double rule2_OversamplingHighClippingValue = std::min(sizeSmallMembership, complexityHighMembership);
  // 3. If RSS is Medium and Complexity is High then Oversampling is High
  double rule3_OversamplingHighClippingValue = std::min(sizeMediumMembership, complexityHighMembership);
  // 4. If RSS is Small and Complexity is Low then Oversampling is Normal
  double rule4_OversamplingNormalClippingValue = std::min(sizeSmallMembership, complexityLowMembership);
  // 5. If RSS is Medium and Complexity is Low then Oversampling is Normal
  double rule5_OversamplingNormalClippingValue = std::min(sizeMediumMembership, complexityLowMembership);
  // 6. If RSS is Large, then Oversampling is Low
  double rule6_OversamplingLowClippingValue = sizeLargeMembership;

  // Determine consequents (clipping output membership functions with rule membership values)
  std::vector<vtkPiecewiseFunction*> consequents;

  vtkSmartPointer<vtkPiecewiseFunction> rule1_oversamplingVeryHigh = vtkSmartPointer<vtkPiecewiseFunction>::New();
  rule1_oversamplingVeryHigh->DeepCopy(oversamplingVeryHigh);
  this->ClipMembershipFunction(rule1_oversamplingVeryHigh, rule1_OversamplingVeryHighClippingValue);
  consequents.push_back(rule1_oversamplingVeryHigh);

  vtkSmartPointer<vtkPiecewiseFunction> rule2_OversamplingHigh = vtkSmartPointer<vtkPiecewiseFunction>::New();
  rule2_OversamplingHigh->DeepCopy(oversamplingHigh);
  this->ClipMembershipFunction(rule2_OversamplingHigh, rule2_OversamplingHighClippingValue);
  consequents.push_back(rule2_OversamplingHigh);

  vtkSmartPointer<vtkPiecewiseFunction> rule3_OversamplingHigh = vtkSmartPointer<vtkPiecewiseFunction>::New();
  rule3_OversamplingHigh->DeepCopy(oversamplingHigh);
  this->ClipMembershipFunction(rule3_OversamplingHigh, rule3_OversamplingHighClippingValue);
  consequents.push_back(rule3_OversamplingHigh);

  vtkSmartPointer<vtkPiecewiseFunction> rule4_OversamplingNormal = vtkSmartPointer<vtkPiecewiseFunction>::New();
  rule4_OversamplingNormal->DeepCopy(oversamplingNormal);
  this->ClipMembershipFunction(rule4_OversamplingNormal, rule4_OversamplingNormalClippingValue);
  consequents.push_back(rule4_OversamplingNormal);

  vtkSmartPointer<vtkPiecewiseFunction> rule5_OversamplingNormal = vtkSmartPointer<vtkPiecewiseFunction>::New();
  rule5_OversamplingNormal->DeepCopy(oversamplingNormal);
  this->ClipMembershipFunction(rule5_OversamplingNormal, rule5_OversamplingNormalClippingValue);
  consequents.push_back(rule5_OversamplingNormal);

  vtkSmartPointer<vtkPiecewiseFunction> rule6_OversamplingLow = vtkSmartPointer<vtkPiecewiseFunction>::New();
  rule6_OversamplingLow->DeepCopy(oversamplingLow);
  this->ClipMembershipFunction(rule6_OversamplingLow, rule6_OversamplingLowClippingValue);
  consequents.push_back(rule6_OversamplingLow);

  // Calculate areas and centroids of all the sections (trapezoids) of all the consequent membership functions
  std::vector<std::pair<double,double> > areaCentroidPairs;
  for (std::vector<vtkPiecewiseFunction*>::iterator consequentIt=consequents.begin(); consequentIt!=consequents.end(); ++consequentIt)
    {
    vtkPiecewiseFunction* currentMembershipFunction = (*consequentIt);

    // Calculate area and center of mass for each consequent
    double currentNode[4] = {0.0,0.0,0.0,0.0};
    double nextNode[4] = {0.0,0.0,0.0,0.0};
    for (int nodeIndex=0; nodeIndex<currentMembershipFunction->GetSize()-1; ++nodeIndex)
      {
      // Calculate area of each trapezoid (may be triangle, rectangle, or actual trapezoid)
      currentMembershipFunction->GetNodeValue(nodeIndex, currentNode);
      currentMembershipFunction->GetNodeValue(nodeIndex+1, nextNode);

      double bottomRectangleArea = (nextNode[0]-currentNode[0]) * std::min(nextNode[1], currentNode[1]);
      double bottomRectangleCentroid = (nextNode[0]+currentNode[0]) / 2.0;

      double topTriangleArea = 0.0;
      double topTriangleCentroid = 0.0;
      if (nextNode[1] > currentNode[1]) // If right node has higher membership
        {
        topTriangleArea = (nextNode[0]-currentNode[0]) * (nextNode[1]-currentNode[1]) / 2.0;
        topTriangleCentroid = currentNode[0] + (nextNode[0]-currentNode[0])*2.0/3.0;
        }
      else if (nextNode[1] < currentNode[1]) // If left node has higher membership (if they are equal then there is no triangle)
        {
        topTriangleArea = (nextNode[0]-currentNode[0]) * (currentNode[1]-nextNode[1]) / 2.0;
        topTriangleCentroid = currentNode[0] + (nextNode[0]-currentNode[0])/3.0;
        }

      double trapezoidArea = bottomRectangleArea + topTriangleArea;
      double trapezoidCentroid = bottomRectangleCentroid;
      if (topTriangleArea > 0.0)
        {
        trapezoidCentroid = ((bottomRectangleArea*bottomRectangleCentroid) + (topTriangleArea*topTriangleCentroid)) / (bottomRectangleArea+topTriangleArea);
        }

      if (trapezoidArea > 0.0) // Only add if area is non-zero
        {
        std::pair<double,double> areaCentroidPair(trapezoidArea,trapezoidCentroid);
        areaCentroidPairs.push_back(areaCentroidPair);
        }
      }
    }

  // Calculate combined center of mass from the components
  double nominator = 0.0;
  double denominator = 0.0;
  for (std::vector<std::pair<double,double> >::iterator trapezoidIt=areaCentroidPairs.begin(); trapezoidIt!=areaCentroidPairs.end(); ++trapezoidIt)
    {
    nominator += trapezoidIt->first * trapezoidIt->second;
    denominator += trapezoidIt->first;
    }
  double centerOfMass = nominator / denominator;

  // Defuzzify output
  double calculatedOversamplingFactorPower = floor(centerOfMass+0.5);

  return pow(2.0,calculatedOversamplingFactorPower);
}

//---------------------------------------------------------------------------
void vtkCalculateOversamplingFactor::ClipMembershipFunction(vtkPiecewiseFunction* membershipFunction, double clipValue)
{
  if (clipValue >= 1.0)
    {
    // No action needed if clip value is greater or equal to one
    return;
    }

  // Find parameter values (strictly between nodes, not at nodes) where membership is
  // exactly the clip value. We will need to create new nodes at those parameter values.
  double currentNode[4] = {0.0,0.0,0.0,0.0};
  double nextNode[4] = {0.0,0.0,0.0,0.0};
  std::vector<double> newNodeParameterValues;
  for (int nodeIndex=0; nodeIndex<membershipFunction->GetSize()-1; ++nodeIndex)
    {
    membershipFunction->GetNodeValue(nodeIndex, currentNode);
    membershipFunction->GetNodeValue(nodeIndex+1, nextNode);
    if ( (currentNode[1] < clipValue && nextNode[1] > clipValue)
      || (currentNode[1] > clipValue && nextNode[1] < clipValue) )
      {
      double newNodeParameterValue = (((nextNode[0]-currentNode[0])*(currentNode[1]-clipValue)) / (currentNode[1]-nextNode[1])) + currentNode[0];
      newNodeParameterValues.push_back(newNodeParameterValue);
      }
    }

  // Move nodes down to clip value that hold value greater than clip value.
  for (int nodeIndex=0; nodeIndex<membershipFunction->GetSize(); ++nodeIndex)
    {
    double currentNode[4] = {0.0,0.0,0.0,0.0};
    membershipFunction->GetNodeValue(nodeIndex, currentNode);
    if (currentNode[1] > clipValue)
      {
      currentNode[1] = clipValue;
      membershipFunction->SetNodeValue(nodeIndex, currentNode);
      }
    }

  // Add new nodes to the clipping points
  for (std::vector<double>::iterator pointIt=newNodeParameterValues.begin(); pointIt!=newNodeParameterValues.end(); ++pointIt)
    {
    membershipFunction->AddPoint(*pointIt, clipValue);
    }
}

//---------------------------------------------------------------------------
void vtkCalculateOversamplingFactor::ApplyOversamplingOnImageGeometry(vtkOrientedImageData* imageData, double oversamplingFactor)
{
  if (!imageData)
    {
    return;
    }

  // Sanity check for sensible oversampling factor
  if ( oversamplingFactor < 0.01
    || oversamplingFactor > 100.0 )
    {
    vtkWarningWithObjectMacro(imageData, "vtkCalculateOversamplingFactor::ApplyOversamplingOnImageGeometry: Oversampling factor" << oversamplingFactor << "seems unreasonable!");
    }
  // Apply oversampling if needed
  else if (oversamplingFactor != 1.0)
    {
    // Calculate extent and spacing
    int newExtent[6] = {0,-1,0,-1,0,-1};
    int extent[6] = {0,-1,0,-1,0,-1};
    imageData->GetExtent(extent);
    double newSpacing[3] = {0.0,0.0,0.0};
    double spacing[3] = {0.0,0.0,0.0};
    imageData->GetSpacing(spacing);
    for (unsigned int axis=0; axis<3; ++axis)
      {
      int dimension = extent[axis*2+1] - extent[axis*2] + 1;
      int extentMin = static_cast<int>(ceil(oversamplingFactor * extent[axis * 2]));
      int extentMax = extentMin + static_cast<int>(floor(oversamplingFactor*dimension)) - 1;
      newExtent[axis*2] = extentMin;
      newExtent[axis*2+1] = extentMax;
      newSpacing[axis] = spacing[axis]
        * double(extent[axis * 2 + 1] - extent[axis * 2] + 1)
        / double(newExtent[axis * 2 + 1] - newExtent[axis * 2] + 1);
      }
    imageData->SetExtent(newExtent);
    imageData->SetSpacing(newSpacing);
    imageData->AllocateScalars(imageData->GetScalarType(), imageData->GetNumberOfScalarComponents());

    // Origin is given in the center of voxels, but we want to have the corners of the new and old volumes
    // to be in the same position, so we need to shift the origin by a half voxel size difference
    vtkSmartPointer<vtkMatrix4x4> imageToWorld = vtkSmartPointer<vtkMatrix4x4>::New();
    imageData->GetImageToWorldMatrix(imageToWorld);
    double newOrigin_Image[4] =
      {
      0.5 * (1 - spacing[0] / newSpacing[0]),
      0.5 * (1 - spacing[1] / newSpacing[1]),
      0.5 * (1 - spacing[2] / newSpacing[2]),
      1.0
      };
    double newOrigin_World[4] = { 0, 0, 0, 1 };
    imageToWorld->MultiplyPoint(newOrigin_Image, newOrigin_World);
    imageData->SetOrigin(newOrigin_World);
    }
}
