#include "Material.h"

// Initialize all static material instances
Material Material::air;           // Will be replaced with MaterialTransparent in Block init
Material Material::ground;
Material Material::wood;
Material Material::rock;
Material Material::iron;
Material Material::water;         // Will be replaced with MaterialLiquid
Material Material::lava;          // Will be replaced with MaterialLiquid
Material Material::leaves;
Material Material::plants;
Material Material::sponge;
Material Material::cloth;
Material Material::fire;
Material Material::sand;
Material Material::circuits;
Material Material::glass;
Material Material::tnt;
Material Material::unused;
Material Material::ice;
Material Material::snow;
Material Material::builtSnow;
Material Material::cactus;
Material Material::clay;
Material Material::pumpkin;
Material Material::portal;

// Note: In the actual initialization, these would be proper subtypes.
// For the server, the key properties are just the flags.
// The static init is done in a source file to avoid multiple definition.
