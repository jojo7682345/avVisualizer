{
	description = "flake description";

	inputs = {
		nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
		avbuilder = {
			type = "github";
			owner = "jojo7682345";
			repo = "avBuilder";
			inputs.nixpkgs.follows = "nixpkgs";
			
		};
	};

	outputs = { self, nixpkgs, ... } @inputs: let
		system = "x86_64-linux";
		pkgs = import nixpkgs { inherit system; };
		avbuilder = inputs.avbuilder.packages.${system}.avbuilder;
	in {
		devShells.${system}.default = pkgs.mkShell {
			nativeBuildInputs = with pkgs; [
				libgcc
				gdb
				glibc.dev
				avbuilder
				vulkan-headers
				vulkan-tools
				vulkan-validation-layers
				vulkan-loader
				shaderc
				shaderc.lib
				spirv-tools
				wayland-protocols
				wayland
			];
			buildInputs = with pkgs; [
			];

			shellHook = ''
  				export CC=gcc
  				export CPATH=${pkgs.glibc.dev}/include:$CPATH
  				export AVBUILDER_HOME=${avbuilder}/share/avBuilder
  				export VULKAN_SDK=${pkgs.vulkan-headers}
  				export CXX=g++
			'';
		};
	};
}
