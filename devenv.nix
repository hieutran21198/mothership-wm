{ lib, pkgs, ... }:
let
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
      pkgs.pixman
      pkgs.libxkbcommon
    ];
    env.CGO_ENABLED = "1";
  };
}
