#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>

extern volatile uint32_t *e1000;

// Device Status Register, offset 8 bytes
#define E1000_STATUS   0x00008

int e1000_attach(struct pci_func *pcif);

#endif  // SOL >= 6
