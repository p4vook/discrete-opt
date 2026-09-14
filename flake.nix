{
  description = "Simple C++ / CMake development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
    in
    {
      devShells = nixpkgs.lib.genAttrs systems (system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          llvm = pkgs.llvmPackages_22;
        in
        {
          default = (pkgs.mkShell.override { stdenv = llvm.stdenv; }  {
            packages = [
              pkgs.git
              pkgs.cmake
              pkgs.ninja
              llvm.clang-tools
            ];
          });
        });
    };
}
