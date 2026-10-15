#pragma once

#include "half.h"
#include "partition.h"
#include "tga.h"

void tex_init(void) {
  half_init();
  partition_init();
  tga_init();
}
