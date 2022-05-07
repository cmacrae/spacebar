{
  description = "A minimal status bar for macOS";

  inputs.nixpkgs.url = github:NixOS/nixpkgs/21.11;
  inputs.flake-utils.url = github:numtide/flake-utils;

  outputs = { self, nixpkgs, flake-utils }:
    {
      overlay = final: prev: { inherit (self.packages.${final.system}) spacebar; };
    }
    // flake-utils.lib.eachSystem [ "aarch64-darwin" "x86_64-darwin" ] (system:
      let pkgs = nixpkgs.legacyPackages.${system}; in
      rec {
        packages = flake-utils.lib.flattenTree {
          spacebar = pkgs.stdenv.mkDerivation rec {
            pname = "spacebar";
            version = "1.4.0";
            src = self;

            buildInputs = with pkgs.darwin.apple_sdk.frameworks; [
              Carbon
              Cocoa
              ScriptingBridge
              SkyLight
            ];

            installPhase = ''
              mkdir -p $out/bin
              mkdir -p $out/share/man/man1/
              cp ./bin/spacebar $out/bin/spacebar
              cp ./doc/spacebar.1 $out/share/man/man1/spacebar.1
            '';

            meta = with pkgs.lib; {
              description = "A minimal status bar for macOS";
              homepage = "https://github.com/cmacrae/spacebar";
              platforms = platforms.darwin;
              maintainers = [ maintainers.cmacrae ];
              license = licenses.mit;
            };
          };
        };

        defaultPackage = packages.spacebar;
        apps.spacebar = flake-utils.lib.mkApp { drv = packages.spacebar; };
        defaultApp = apps.spacebar;

        devShell = pkgs.mkShell {
          name = "spacebar";
          inputsFrom = [ packages.spacebar ];
          buildInputs = [ pkgs.asciidoctor ];
        };
      });
}
