#pragma once

#include "TileEntity.h"
#include "../core/NBT.h"
#include <array>
#include <string>

// Stores the 4 lines of text on a sign block
class TileEntitySign : public TileEntity {
public:
    std::array<std::string, 4> signText{};

    std::string getEntityId() const override { return "Sign"; }

    void readFromNBT(const NBTCompound& nbt) override {
        TileEntity::readFromNBT(nbt);
        for (int i = 0; i < 4; ++i) {
            signText[i] = nbt.getString("Text" + std::to_string(i + 1));
            // Java truncates to 15 chars
            if (signText[i].size() > 15)
                signText[i] = signText[i].substr(0, 15);
        }
    }

    void writeToNBT(NBTCompound& nbt) const override {
        TileEntity::writeToNBT(nbt);
        for (int i = 0; i < 4; ++i)
            nbt.setString("Text" + std::to_string(i + 1), signText[i]);
    }
};

REGISTER_TILE_ENTITY(TileEntitySign, "Sign");
