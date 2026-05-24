#include <wayland-client-core.h>
#include <wayland-client.h>
#include "logging.h"
#include "xdg-shell-client-protocol.h"


void waylandPluginMain(){

	struct wl_display* display = wl_display_connect(NULL);
	if(display){
		avDebug("Connected to display");
	} else{
		avError("Error connecting to display");
		return;
	}
	wl_display_disconnect(display);

}