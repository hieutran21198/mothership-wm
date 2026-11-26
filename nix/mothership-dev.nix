{ config, lib, pkgs, ... }:
let
  cfg = config.programs.mothershipDev;
  gst = pkgs.gst_all_1;
  launcher = pkgs.writeShellScriptBin "mothership-dev-session" ''
    set -eu
    if [ "$(${pkgs.coreutils}/bin/id -u)" -eq 0 ]; then
      echo 'Mothership Dev must run as the logged-in user.' >&2
      exit 1
    fi
    state="''${XDG_STATE_HOME:-$HOME/.local/state}/mothership-dev"
    candidate="$(${pkgs.coreutils}/bin/realpath -e -- "$state/current" 2>/dev/null || true)"
    if [ -z "$candidate" ] || [ ! -f "$candidate/.mothership-candidate" ]; then
      echo 'No Mothership Dev build selected. Select a tested candidate before login.' >&2
      exit 1
    fi
    for part in mothership mothership-wm mothership-desktopd mothership-command-center; do
      if [ ! -f "$candidate/$part" ] || [ ! -x "$candidate/$part" ]; then
        echo "Mothership Dev build is missing: $part" >&2
        exit 1
      fi
    done
    unset MOTHERSHIP_NATIVE_TEST_HEADLESS
    export XDG_SESSION_TYPE=wayland
    export XDG_CURRENT_DESKTOP=Mothership
    export XDG_SESSION_DESKTOP=mothership-dev
    export XKB_CONFIG_ROOT="''${XKB_CONFIG_ROOT:-${pkgs.xkeyboard_config}/share/X11/xkb}"
    export MOTHERSHIP_GST_LIBRARY="${lib.getLib gst.gstreamer}/lib/libgstreamer-1.0.so.0"
    export GST_PLUGIN_PATH_1_0="${lib.makeSearchPath "lib/gstreamer-1.0" [
      gst.gstreamer gst.gst-plugins-base gst.gst-plugins-good gst.gst-plugins-bad
      gst.gst-libav gst.gst-plugins-rs
    ]}"
    exec "$candidate/mothership" --native
  '';
  session = pkgs.runCommand "mothership-dev-session" {
    passthru.providedSessions = [ "mothership-dev" ];
  } ''
    mkdir -p "$out/share/wayland-sessions"
    cat > "$out/share/wayland-sessions/mothership-dev.desktop" <<'DESKTOP'
    [Desktop Entry]
    Name=Mothership Dev
    Comment=Run the selected local Mothership build
    Exec=${launcher}/bin/mothership-dev-session
    Type=Application
    DesktopNames=Mothership
    DESKTOP
  '';
in
{
  options.programs.mothershipDev.enable = lib.mkEnableOption "the opt-in Mothership Dev Wayland login";
  config = lib.mkIf cfg.enable {
    services.displayManager.sessionPackages = [ session ];
  };
}
