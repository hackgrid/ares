#if defined(PROFILE_PERFORMANCE)
#include "../vdp-performance/vdp.cpp"
#else
#include <md/md.hpp>

namespace ares::MegaDrive {

VDP vdp;
#include "memory.cpp"
#include "io.cpp"
#include "dma.cpp"
#include "render.cpp"
#include "background.cpp"
#include "sprite.cpp"
#include "color.cpp"
#include "serialization.cpp"

auto VDP::load(Node::Object parent, Node::Object from) -> void {
  node = Node::append<Node::Component>(parent, from, "VDP");
  from = Node::scan(parent = node, from);

  screen = Node::append<Node::Screen>(parent, from, "Screen");
  screen->colors(3 * (1 << 9), {&VDP::color, this});
  screen->setSize(1280, 480);
  screen->setScale(0.25, 0.50);
  screen->setAspect(1.0, 1.0);
  from = Node::scan(parent = screen, from);

  overscan = Node::append<Node::Boolean>(parent, from, "Overscan", true, [&](auto value) {
    if(value == 0) screen->setSize(1280, 448);
    if(value == 1) screen->setSize(1280, 480);
  });
  overscan->setDynamic(true);

  debugVRAM = Node::append<Node::Memory>(parent, from, "VDP VRAM");
  debugVRAM->setSize(32_KiB << 1);
  debugVRAM->setRead([&](uint32 address) -> uint8 {
    return vram.memory[uint15(address >> 1)].byte(!address.bit(0));
  });
  debugVRAM->setWrite([&](uint32 address, uint8 data) -> void {
    auto value = vram.memory[uint15(address >> 1)];
    value.byte(!address.bit(0)) = data;
    vram.memory[uint15(address >> 1)] = value;
  });

  debugVSRAM = Node::append<Node::Memory>(parent, from, "VDP VSRAM");
  debugVSRAM->setSize(40 << 1);
  debugVSRAM->setRead([&](uint32 address) -> uint8 {
    if(address >= 40 << 1) return 0x00;
    return vsram.memory[address >> 1].byte(!address.bit(0));
  });
  debugVSRAM->setWrite([&](uint32 address, uint8 data) -> void {
    if(address >= 40 << 1) return;
    auto value = vsram.memory[address >> 1];
    value.byte(!address.bit(0)) = data;
    vsram.memory[address >> 1] = value;
  });

  debugCRAM = Node::append<Node::Memory>(parent, from, "VDP CRAM");
  debugCRAM->setSize(64 << 1);
  debugCRAM->setRead([&](uint32 address) -> uint8 {
    return cram.memory[uint6(address >> 1)].byte(!address.bit(0));
  });
  debugCRAM->setWrite([&](uint32 address, uint8 data) -> void {
    auto value = cram.memory[uint6(address >> 1)];
    value.byte(!address.bit(0)) = data;
    cram.memory[uint6(address >> 1)] = value;
  });
}

auto VDP::unload() -> void {
  node = {};
  screen = {};
  overscan = {};
  debugVRAM = {};
  debugVSRAM = {};
  debugCRAM = {};
}

auto VDP::main() -> void {
  scanline();

  cpu.lower(CPU::Interrupt::HorizontalBlank);
  apu.setINT(false);

  if(state.vcounter == 0) {
    latch.horizontalInterruptCounter = io.horizontalInterruptCounter;
    io.vblankIRQ = false;
    cpu.lower(CPU::Interrupt::VerticalBlank);
  }

  if(state.vcounter == screenHeight()) {
    if(io.verticalBlankInterruptEnable) {
      io.vblankIRQ = true;
      cpu.raise(CPU::Interrupt::VerticalBlank);
    }
    //todo: should only stay high for ~2573/2 clocks
    apu.setINT(true);
  }

  if(state.vcounter < screenHeight()) {
    while(state.hcounter < 1280) {
      run();
      state.hdot++;
      step(pixelWidth());
    }

    if(latch.horizontalInterruptCounter-- == 0) {
      latch.horizontalInterruptCounter = io.horizontalInterruptCounter;
      if(io.horizontalBlankInterruptEnable) {
        cpu.raise(CPU::Interrupt::HorizontalBlank);
      }
    }

    step(430);
  } else {
    step(1710);
  }

  state.hdot = 0;
  state.hcounter = 0;
  if(++state.vcounter >= frameHeight()) {
    state.vcounter = 0;
    state.field ^= 1;
    latch.overscan = io.overscan;
  }
  latch.displayWidth = io.displayWidth;
}

auto VDP::step(uint clocks) -> void {
  state.hcounter += clocks;
  while(clocks--) {
    dma.run();
    Thread::step(1);
    Thread::synchronize(cpu, apu);
  }
}

auto VDP::refresh() -> void {
  auto data = output;

  if(overscan->value() == 0) {
    if(latch.overscan) data += 16 * 1280;
    screen->refresh(data, 1280 * sizeof(uint32), 1280, 448);
  }

  if(overscan->value() == 1) {
    if(!latch.overscan) data -= 16 * 1280;
    screen->refresh(data, 1280 * sizeof(uint32), 1280, 480);
  }
}

auto VDP::power(bool reset) -> void {
  Thread::create(system.frequency() / 2.0, {&VDP::main, this});

  output = buffer + 16 * 1280;  //overscan offset

  if(!reset) {
    for(auto& data : vram.memory) data = 0;
    for(auto& data : vsram.memory) data = 0;
    for(auto& data : cram.memory) data = 0;
  }

  vram.mode = 0;
  io = {};
  latch = {};
  state = {};

  planeA.power();
  window.power();
  planeB.power();
  sprite.power();
  dma.power();
}

}
#endif