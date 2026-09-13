{
  description = "BeSTspeech / Keynote GOLD reimplementation";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAll = f: nixpkgs.lib.genAttrs systems (s: f nixpkgs.legacyPackages.${s});
    in
    {
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
