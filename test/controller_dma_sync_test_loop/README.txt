Modified versions of dma_sync_test_v2 for testing longer synced regions and interaction of DMC DMA with write cycles. See discussion here for details: https://forums.nesdev.com/viewtopic.php?f=2&t=14319&start=60#p231503

Both of these tests rely on the guarantee that the cycle after OAM DMA is even and that DMC DMA attempts to engage only on odd cycles. Joypad reads are timed so that the read cycle is even, preventing collision with DMC DMA that would cause joypad bit deletion. These tests read the joypads repeatedly without ever syncing again via OAM DMA, so they must remain perfectly synced indefinitely to avoid bit deletion. However, if DMC DMA lands on a write cycle, it tries to engage again on the very next cycle, which may be even. If it engages on an even cycle, it will last 3 cycles instead of its usual 4, changing cycle parity within the joypad reading code and causing future bit deletion.

badrol contains a ROL instruction that is aligned to cause parity flip to happen. This test is expected to fail (screen turns white). 

goodrol fixes the alignment of the ROL instruction to maintain parity and is expected to succeed (screen stays black until right is manually pressed).


To assemble these tests, use snarfblasm.exe.