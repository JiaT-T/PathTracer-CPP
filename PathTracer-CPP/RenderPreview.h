#pragma once

#include <vector>
#include "Color.h"

// References are valid only during Update. A preview retaining frames must copy them.
// An empty denoised buffer means no new filtered snapshot; final clears any stale one.
struct ProgressivePreviewFrame
{
	const std::vector<Color>& raw;
	const std::vector<Color>& denoised;
	int completed_samples;
	int total_samples;
	double elapsed_seconds;
	bool final;
};

// Decouples pass publication from the Win32 window (also usable by headless consumers).
class RenderPreview
{
public:
	virtual ~RenderPreview() = default;
	virtual void Update(const ProgressivePreviewFrame& frame) = 0;
	virtual bool IsClosed() const = 0;
	virtual DisplaySettings GetDisplaySettings() const = 0;
};
