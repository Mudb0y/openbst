{
  description = "BeSTspeech / Keynote GOLD reimplementation";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      # The package's licence says the tables are not ours to license, which
      # makes it unfree, which would otherwise stop it building here. Allowing
      # it is this flake's own decision and does not change what the metadata
      # says to anyone who takes the package into their own package set.
      pkgsFor = system: import nixpkgs {
        inherit system;
        config.allowUnfree = true;
      };
      forAll = f: nixpkgs.lib.genAttrs systems (s: f (pkgsFor s));
    in
    {
      packages = forAll (pkgs: rec {
        default = bestspeech;

        bestspeech = pkgs.stdenv.mkDerivation {
          pname = "bestspeech";
          version = "0.1.0";

          # The flake's source is the git tree, so the original binaries under
          # dll, which are not tracked, are not here. The build does not want
          # them: the tables it speaks from are under src/data.
          src = ./.;

          enableParallelBuilding = true;
          makeFlags = [ "PREFIX=${placeholder "out"}" ];

          # The one test that needs no original binary.
          doCheck = true;
          checkTarget = "selftest";

          meta = with pkgs.lib; {
            description = "The Berkeley Speech Technologies synthesizer, reimplemented";
            homepage = "https://github.com/Mudb0y/bestspeech";
            mainProgram = "bstspeak";
            platforms = platforms.unix;
            # Our code is MIT. The tables under src/data are Berkeley Speech
            # Technologies' and we are in no position to license them, which
            # is what the second entry says and why this counts as unfree.
            # See LICENSE.
            license = [
              licenses.mit
              {
                shortName = "bst-tables";
                fullName = "Berkeley Speech Technologies' tables, not ours to license";
                free = false;
              }
            ];
          };
        };
      });

      apps = forAll (pkgs: {
        default = {
          type = "app";
          program = "${self.packages.${pkgs.stdenv.hostPlatform.system}.bestspeech}/bin/bstspeak";
        };
      });

      devShells = forAll (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            gcc
            gnumake
            pkg-config
            unicorn
            capstone
            gdb
            rizin
            ghidra
            (python3.withPackages (ps: with ps; [ unicorn capstone numpy ]))
          ];
        };
      });
    };
}
