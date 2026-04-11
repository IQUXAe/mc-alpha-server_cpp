#pragma once

#include "../Packet.h"
#include "../../world/TileEntity.h"
#include "../../core/NBT.h"
#include <vector>
#include <memory>

// Packet for synchronizing TileEntity data to clients (chests, furnaces, signs, etc.)
// Matches Java's Packet59ComplexEntity exactly
class Packet59ComplexEntity : public Packet {
public:
    int xPosition = 0;
    int yPosition = 0;
    int zPosition = 0;
    std::vector<uint8_t> entityData;

    Packet59ComplexEntity() {
        isChunkDataPacket = true;
    }

    // Constructor for creating packet from TileEntity
    Packet59ComplexEntity(int x, int y, int z, TileEntity* tileEntity) 
        : xPosition(x), yPosition(y), zPosition(z) {
        isChunkDataPacket = true;
        
        if (tileEntity) {
            // Serialize TileEntity to NBT
            NBTCompound nbt;
            tileEntity->writeToNBT(nbt);
            
            // Write NBT to buffer (uncompressed, matching Java)
            ByteBuffer buf;
            nbt.writeRoot(buf, ""); // Empty root name
            
            entityData = buf.data;
        }
    }

    void readPacketData(ByteBuffer& buf) override {
        xPosition = buf.readInt();
        yPosition = buf.readShort();
        zPosition = buf.readInt();
        
        uint16_t dataLength = buf.readShort() & 0xFFFF;
        entityData.resize(dataLength);
        buf.readBytes(entityData.data(), dataLength);
    }

    void writePacketData(ByteBuffer& buf) override {
        buf.writeInt(xPosition);
        buf.writeShort(static_cast<int16_t>(yPosition));
        buf.writeInt(zPosition);
        buf.writeShort(static_cast<int16_t>(entityData.size()));
        buf.writeBytes(entityData);
    }

    int getPacketSize() override {
        return 10 + 2 + static_cast<int>(entityData.size()); // 4+2+4 coords + 2 length + data
    }

    uint8_t getPacketId() override {
        return 0x3B; // 59 in hex
    }
};
