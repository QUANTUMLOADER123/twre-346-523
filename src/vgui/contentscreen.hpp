// contentscreen.hpp - first-start loading screen that downloads the CS:S content
#pragma once
// Runs its own frame loop until the content is installed. Returns false when the user skipped / closed the window.
bool ContentScreen_Run(bool force = false);
