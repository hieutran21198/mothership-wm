#ifndef MOTHERSHIP_VIDEO_FROST_SAMPLE_H
#define MOTHERSHIP_VIDEO_FROST_SAMPLE_H

#include <math.h>
#include <stdbool.h>
#include <wlr/util/box.h>

// Clip each shifted tap to the visible part of the texture, not to
// the Map frame. Small margins can leave no padding on the outside.
static inline bool video_frost_sample(struct wlr_fbox *result,struct wlr_fbox box,
		struct wlr_fbox visible,int width,int height) {
	if (!(width>0 && height>0 && isfinite(box.x) && isfinite(box.y) &&
			isfinite(box.width) && isfinite(box.height) &&
			isfinite(visible.x) && isfinite(visible.y) &&
			isfinite(visible.width) && isfinite(visible.height))) return false;
	double l=fmax(fmax(0,visible.x),box.x);
	double t=fmax(fmax(0,visible.y),box.y);
	double r=fmin(fmin(width,visible.x+visible.width),box.x+box.width);
	double b=fmin(fmin(height,visible.y+visible.height),box.y+box.height);
	if (!(l<r && t<b)) return false;
	*result=(struct wlr_fbox){l,t,r-l,b-t};
	return true;
}

#endif
