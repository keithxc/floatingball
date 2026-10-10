{
  description = "Floating screenshot workspace and desktop shortcuts";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/151fa4e8ddfdd8dd25d945ad94ed54a13de9f6e4";

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      floatingball = pkgs.stdenv.mkDerivation {
        pname = "floatingball";
        version = "0.1.0";
        src = pkgs.lib.cleanSourceWith {
          src = ./.;
          filter = path: type: pkgs.lib.cleanSourceFilter path type
            && builtins.baseNameOf path != "build";
        };
        nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config pkgs.qt6.wrapQtAppsHook ];
        buildInputs = [
          pkgs.qt6.qtbase pkgs.qt6.qttools pkgs.qt6.qtwayland
          pkgs.qt6.qtimageformats pkgs.kdePackages.layer-shell-qt
        ];
        cmakeFlags = [ "-DFLOATINGBALL_BUILD_TESTS=ON" ];
        doCheck = true;
        checkPhase = ''
          runHook preCheck
          QT_QPA_PLATFORM=offscreen ctest --output-on-failure
          runHook postCheck
        '';
        meta.mainProgram = "FloatingBall";
      };
    in {
      packages.${system}.default = floatingball;
      devShells.${system}.default = pkgs.mkShell { inputsFrom = [ floatingball ]; };
    };
}
