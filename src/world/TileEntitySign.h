#pragma once

#include "TileEntity.h"
#include "../core/NBT.h"
#include "../core/RustBridge.h"

// Stores the 4 lines of text on a sign block (15 chars max per line)
class TileEntitySign : public TileEntity {
public:
    TileEntitySign() {
        state_ = RustBridge::signCreate();
    }

    ~TileEntitySign() override = default;

    std::string getEntityId() const override { return "Sign"; }

    const char* getLine(int idx) const {
        if (idx < 0 || idx >= 4) return "";
        return reinterpret_cast<const char*>(state_.lines[idx]);
    }

    void setLine(int idx, const char* text) {
        if (idx < 0 || idx >= 4 || !text) return;
        RustBridge::signSetLine(&state_, idx, text);
    }

    void readFromNBT(const NBTCompound& nbt) override {
        TileEntity::readFromNBT(nbt);
        for (int i = 0; i < 4; ++i) {
            std::string line = nbt.getString("Text" + std::to_string(i + 1));
            if (line.size() > 15)
                line = line.substr(0, 15);
            setLine(i, line.c_str());
        }
    }

    void writeToNBT(NBTCompound& nbt) const override {
        TileEntity::writeToNBT(nbt);
        for (int i = 0; i < 4; ++i)
            nbt.setString("Text" + std::to_string(i + 1), reinterpret_cast<const char*>(state_.lines[i]));
    }

private:
    RustBridge::FfiSignState state_;
};

REGISTER_TILE_ENTITY(TileEntitySign, "Sign");
