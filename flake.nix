{
  description = "wheel-hid-emulator";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };
  outputs = { self, nixpkgs }: let
    systems = [ "x86_64-linux" ];
  in {
    packages = builtins.listToAttrs (map (system: let pkgs = import nixpkgs { inherit system; }; in {
      name = system;
      value = pkgs.stdenv.mkDerivation {
        pname = "wheel-hid-emulator";
        version = builtins.getEnv "GIT_COMMIT";
        src = ./.;
        nativeBuildInputs = [ pkgs.pkg-config ];
        buildInputs = [ pkgs.hidapi ];
        buildPhase = "make";
        installPhase = ''
          mkdir -p $out/bin
          install -Dm755 wheel-emulator $out/bin/wheel-emulator
        '';
        meta = {
          license = pkgs.lib.licenses.gpl2Only;
          description = "wheel-hid-emulator";
        };
      };
    }) systems);
    defaultPackage.x86_64-linux = self.packages.x86_64-linux;
    devShells.x86_64-linux.default = import nixpkgs { system = "x86_64-linux"; }.mkShell { buildInputs = [ (import nixpkgs { system = "x86_64-linux"; }).pkg-config ]; };
  };
}
