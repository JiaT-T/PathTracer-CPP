#pragma once
#include <string>

struct RegressionOptions
{
	bool update_references = false;
	int reference_spp = 0; // 0 = 16x the test spp of each scene
	std::string reference_dir = "tests/reference";
};

// Numerical unit tests (PDF normalization, BSDF sample/eval/pdf consistency, white furnace,
// texture decoding, determinism). Returns the number of failed checks (0 = success).
int run_unit_tests(const std::string& filter);

// Renders the small fixed-seed regression scenes and compares them to reference PFMs.
int run_regression(const RegressionOptions& options);

struct DenoiseEvalOptions
{
	std::string scene;      // empty = a default set of scenes
	int spp = 32;
	int reference_spp = 0;  // 0 = 64x spp
	int width = 0;          // 0 = scene default
	bool sweep = false;     // also report a sigma_luminance x iterations sweep
};

// Error (relMSE / RMSE vs a high-spp reference) of the raw image, the legacy filter and the
// variance-guided filter at the same spp.
int run_denoise_eval(const DenoiseEvalOptions& options);
