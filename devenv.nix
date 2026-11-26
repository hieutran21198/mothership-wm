{ lib, pkgs, ... }:
let
  gst = pkgs.gst_all_1;
  wallpaperPlugins = [ gst.gstreamer gst.gst-plugins-base gst.gst-plugins-good
    gst.gst-plugins-bad gst.gst-libav gst.gst-plugins-rs ];
  pins = builtins.filter (pin: pin != null) (
    map (builtins.match "go ([0-9]+[.][0-9]+[.][0-9]+)") (
      lib.splitString "\n" (builtins.readFile ./go.mod)
    )
  );
  goVersion =
    if builtins.length pins == 1
    then builtins.head (builtins.head pins)
    else throw "go.mod must contain exactly one full Go version.";
in
{
  languages.go = {
    enable = true;
    version = goVersion;
    lsp.enable = true;
  };

  packages = [ pkgs.go-task ];
  dotenv.enable = false;
  env.GOTOOLCHAIN = "local";

  # Keep the unstable wlroots C ABI explicit and out of the default shell.
  profiles.wm-native.module = {
    packages = [
      pkgs.pkg-config
      pkgs.stdenv.cc
      pkgs.wlroots_0_20
      pkgs.wayland
      pkgs.wayland-protocols
      pkgs.wayland-scanner
      pkgs.pixman
      pkgs.libxkbcommon
      pkgs.cairo
      pkgs.pango
      pkgs.libdrm
      pkgs.libglvnd
      pkgs.glib
    ];
    env.CGO_ENABLED = "1";
  };

  profiles.panel-native.module = {
    packages = [ pkgs.pkg-config pkgs.stdenv.cc pkgs.gtk4 pkgs.librsvg pkgs.libepoxy pkgs.wayland pkgs.wayland-protocols pkgs.wayland-scanner ];
    env.MOTHERSHIP_GST_LIBRARY = "${lib.getLib gst.gstreamer}/lib/libgstreamer-1.0.so.0";
    env.GST_PLUGIN_PATH_1_0 = lib.makeSearchPath "lib/gstreamer-1.0" wallpaperPlugins;
    # The stable layer-shell runtime supplies the linker symlink; the panel
    # uses a small ABI shim, not a second toolkit binding.
    env.CGO_ENABLED = "1";
    env.CGO_LDFLAGS = "-L${lib.getLib pkgs.gtk4-layer-shell}/lib -Wl,-rpath,${lib.getLib pkgs.gtk4-layer-shell}/lib";
  };

}
