{
  description = "wheel-hid-emulator";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs";
  outputs = { self, nixpkgs }:
    let pkgs = import nixpkgs { system="x86_64-linux"; }; in {
      packages.x86_64-linux.wheel-hid-emulator = pkgs.stdenv.mkDerivation {
        pname="wheel-hid-emulator"; version="main"; src=./.;
        nativeBuildInputs=[pkgs.pkg-config]; buildInputs=[pkgs.hidapi];
        buildPhase="make"; installPhase='mkdir -p $out/bin; install -m755 wheel-emulator $out/bin/';
      };
      defaultPackage.x86_64-linux = packages.x86_64-linux.wheel-hid-emulator;
    };
}
