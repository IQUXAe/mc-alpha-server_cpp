#pragma once

class Material {
public:
    constexpr Material(bool isLiquid = false, bool isSolid = true,
                       bool canBlockGrass = true, bool blocksMovement = true)
        : isLiquid_(isLiquid),
          isSolid_(isSolid),
          canBlockGrass_(canBlockGrass),
          blocksMovement_(blocksMovement) {}

    virtual ~Material() = default;

    virtual bool getIsLiquid() const { return isLiquid_; }
    virtual bool isSolid() const { return isSolid_; }
    virtual bool getCanBlockGrass() const { return canBlockGrass_; }
    virtual bool blocksMovement() const { return blocksMovement_; }
    bool getBurning() const { return canBurn_; }

    Material& setBurning() { canBurn_ = true; return *this; }

    // Static material instances
    static Material air;
    static Material ground;
    static Material wood;
    static Material rock;
    static Material iron;
    static Material water;
    static Material lava;
    static Material leaves;
    static Material plants;
    static Material sponge;
    static Material cloth;
    static Material fire;
    static Material sand;
    static Material circuits;
    static Material glass;
    static Material tnt;
    static Material unused;
    static Material ice;
    static Material snow;
    static Material builtSnow;
    static Material cactus;
    static Material clay;
    static Material pumpkin;
    static Material portal;
    static Material web;

private:
    bool isLiquid_ = false;
    bool isSolid_ = true;
    bool canBlockGrass_ = true;
    bool blocksMovement_ = true;
    bool canBurn_ = false;
};

class MaterialTransparent : public Material {
public:
    constexpr MaterialTransparent() : Material(false, false, false, false) {}
};

class MaterialLiquid : public Material {
public:
    constexpr MaterialLiquid() : Material(true, false, true, false) {}
};

class MaterialLogic : public Material {
public:
    constexpr MaterialLogic() : Material(false, false, false, false) {}
};
