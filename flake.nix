{
  description = "Touch-first desktop floating ball (Xiaomi Mi 5 Quick Ball + fooView Crosshair)";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/151fa4e8ddfdd8dd25d945ad94ed54a13de9f6e4";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-darwin" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs { inherit system; }));
    in {
      packages = forAllSystems (pkgs:
        let
          isLinux = pkgs.stdenv.hostPlatform.isLinux;
        in {
          default = pkgs.stdenv.mkDerivation {
            pname = "floatingball";
            version = "0.2.0";
            src = pkgs.lib.cleanSourceWith {
              src = ./.;
              filter = path: type: pkgs.lib.cleanSourceFilter path type
                && builtins.baseNameOf path != "build";
            };
            nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config pkgs.qt6.wrapQtAppsHook ];
            buildInputs = [
              pkgs.qt6.qtbase
              pkgs.qt6.qttools
              pkgs.qt6.qtimageformats
            ] ++ pkgs.lib.optionals isLinux [
              pkgs.qt6.qtwayland
              pkgs.kdePackages.layer-shell-qt
            ];
            cmakeFlags = [ "-DFLOATINGBALL_BUILD_TESTS=ON" ]
              ++ pkgs.lib.optionals pkgs.stdenv.hostPlatform.isDarwin [ "-DFLOATINGBALL_NATIVE_APP_ICON=OFF" ];
            qtWrapperArgs = pkgs.lib.optionals isLinux [
              "--prefix" "PATH" ":" "${pkgs.lib.makeBinPath [ pkgs.wireplumber pkgs.pulseaudio pkgs.dbus pkgs.wtype ]}"
              "--prefix" "XDG_DATA_DIRS" ":" "${pkgs.kdePackages.breeze-icons}/share:${pkgs.hicolor-icon-theme}/share"
            ];
            doCheck = true;
            checkPhase = ''
              runHook preCheck
              export HOME="$TMPDIR/floatingball-test-home"
              mkdir -p "$HOME"
              QT_QPA_PLATFORM=offscreen ctest --output-on-failure
              runHook postCheck
            '';
            postInstall = pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
              mkdir -p "$out/Applications"
              if [ -d "$out/FloatingBall.app" ]; then
                mv "$out/FloatingBall.app" "$out/Applications/FloatingBall.app"
                ln -s Applications/FloatingBall.app "$out/FloatingBall.app"
              fi
            '';
            postFixup = pkgs.lib.optionalString pkgs.stdenv.hostPlatform.isDarwin ''
              mkdir -p "$out/bin"
              ln -sf ../Applications/FloatingBall.app/Contents/MacOS/FloatingBall "$out/bin/FloatingBall"
            '';
            meta.mainProgram = "FloatingBall";
          };
        });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          inputsFrom = [ self.packages.${pkgs.stdenv.hostPlatform.system}.default ];
        };
      });
    };
}
