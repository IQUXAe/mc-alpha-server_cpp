#pragma once

class Material {
public:
    virtual ~Material() = default;

    virtual bool getIsLiquid() const { return false; }
    virtual bool isSolid() const { return true; }
    virtual bool getCanBlockGrass() const { return true; }
    virtual bool blocksMovement() const { return true; }
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

private:
    bool canBurn_ = false;
};

class MaterialTransparent : public Material {
public:
    bool isSolid() const override { return false; }
    bool getCanBlockGrass() const override { return false; }
    bool blocksMovement() const override { return false; }
};

class MaterialLiquid : public Material {
public:
    bool getIsLiquid() const override { return true; }
    bool isSolid() const override { return false; }
    bool blocksMovement() const override { return false; }
};

class MaterialLogic : public Material {
public:
    bool isSolid() const override { return false; }
    bool getCanBlockGrass() const override { return false; }
    bool blocksMovement() const override { return false; }
};
