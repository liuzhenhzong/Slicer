/*=========================================================================

  Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

==========================================================================*/

#ifndef __vtkITKMorphologicalContourInterpolator_h
#define __vtkITKMorphologicalContourInterpolator_h

#include "vtkITK.h"
#include "vtkSimpleImageToImageFilter.h"

/// \brief Wrapper class around itk::MorphologicalContourInterpolator.
class VTK_ITK_EXPORT vtkITKMorphologicalContourInterpolator : public vtkSimpleImageToImageFilter
{
public:
  static vtkITKMorphologicalContourInterpolator *New();
  vtkTypeMacro(vtkITKMorphologicalContourInterpolator, vtkSimpleImageToImageFilter);
  void PrintSelf(ostream& os, vtkIndent indent);

  /// Interpolate only this label. Interpolates all labels if set to 0 (default).
  vtkGetMacro(Label, long);
  vtkSetMacro(Label, long);

  /// Interpolate only along this axis. Interpolates along all axes if set to -1 (default).
  vtkGetMacro(Axis, int);
  vtkSetMacro(Axis, int);

  /// Heuristic alignment of regions for interpolation is faster than optimal alignment.
  /// Default is heuristic.
  vtkGetMacro(HeuristicAlignment, bool);
  vtkSetMacro(HeuristicAlignment, bool);

  /// Using distance transform instead of repeated dilations to calculate
  /// the median contour is slightly faster, but produces lower quality interpolations.
  /// Default is OFF(that is, use repeated dilations).
  vtkGetMacro(UseDistanceTransform, bool);
  vtkSetMacro(UseDistanceTransform, bool);

  /// Use ball instead of default cross structuring element for repeated dilations.
  vtkGetMacro(UseBallStructuringElement, bool);
  vtkSetMacro(UseBallStructuringElement, bool);

protected:
  vtkITKMorphologicalContourInterpolator();
  ~vtkITKMorphologicalContourInterpolator();

  virtual void SimpleExecute(vtkImageData* input, vtkImageData* output);

  long Label;
  int Axis;
  bool HeuristicAlignment;
  bool UseDistanceTransform;
  bool UseBallStructuringElement;

private:
  vtkITKMorphologicalContourInterpolator(const vtkITKMorphologicalContourInterpolator&);  /// Not implemented.
  void operator=(const vtkITKMorphologicalContourInterpolator&);  /// Not implemented.
};

#endif
