#include <assert.h>
#include "../video-frost-sample.h"
#include "../../../internal/wayland/map-frame.h"

static void within(struct wlr_fbox box,struct wlr_fbox visible,int tw,int th) {
	struct wlr_fbox out;
	assert(video_frost_sample(&out,box,visible,tw,th));
	assert(out.x>=fmax(0,visible.x) && out.y>=fmax(0,visible.y));
	assert(out.width>0 && out.height>0);
	assert(out.x+out.width<=fmin(tw,visible.x+visible.width)+1e-9);
	assert(out.y+out.height<=fmin(th,visible.y+visible.height)+1e-9);
}

int main(void) {
	struct wlr_fbox box={6,7,12,13},out;
	assert(video_frost_sample(&out,box,(struct wlr_fbox){0,0,20,25},20,25));
	assert(box.x==out.x && box.y==out.y && box.width==out.width && box.height==out.height);
	assert(!video_frost_sample(&out,(struct wlr_fbox){-20,-5,10,10},box,20,25));
	assert(!video_frost_sample(&out,(struct wlr_fbox){NAN,0,10,10},box,20,25));

	const int sizes[][2]={{1600,900},{2560,1440},{1279,719},{720,1366},{320,240}};
	for(unsigned mode=0;mode<sizeof sizes/sizeof sizes[0];mode++) {
		int w=sizes[mode][0],h=sizes[mode][1];
		int tw=(int)ceil(w*1.5),th=(int)ceil(h*1.5);
		const struct wlr_fbox visible={.25,.50,w*1.5-.5,h*1.5-1};
		double scale=fmin(960./w,540./h);
		for(int margin=0;margin<=96;margin++) {
			struct map_frame_box view=map_frame_layout(w,h,margin);
			int ow=(int)(view.width*scale),oh=(int)(view.height*scale);
			if (ow<=2*ceil(MAP_FRAME_CUT*scale) || oh<=2*ceil(MAP_FRAME_CUT*scale)) continue;
			double px=visible.width/w,py=visible.height/h,pad=8*view.height/oh;
			for(int i=-2;i<=2;i++) {
				within((struct wlr_fbox){visible.x+(view.x+i*view.width/ow*2)*px,
					visible.y+(view.y-pad)*py,view.width*px,(view.height+2*pad)*py},visible,tw,th);
				within((struct wlr_fbox){0,8+i*2,ow,oh},(struct wlr_fbox){0,0,ow,oh+16},ow,oh+16);
			}
		}
	}
}
