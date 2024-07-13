Subject: [PATCH V14 0/7] Add architecture agnostic code to support vCPU Hotplug
Date: Fri, 12 Jul 2024 14:41:54 +0100	[thread overview]

Virtual CPU hotplug support is being added across various architectures [1][3].
This series adds various code bits common across all architectures:

1. vCPU creation and Parking code refactor [Patch 1]
2. Update ACPI GED framework to support vCPU Hotplug [Patch 2,3]
3. ACPI CPUs AML code change [Patch 4,5]
4. Helper functions to support unrealization of CPU objects [Patch 6,7]

Repository:

[*] Architecture *Agnostic* Patch-set (This series)
   V14: https://github.com/salil-mehta/qemu.git virt-cpuhp-armv8/rfc-v3.arch.agnostic.v14

   NOTE: This series is meant to work in conjunction with the architecture-specific
   patch-set. For ARM, a combined patch-set (architecture agnostic + specific) was
   earlier pushed as RFC V2 [1]. Later, RFC V2 was split into the ARM Architecture
   specific patch-set RFC V3 [4] (a subset of RFC V2) and the architecture agnostic
   patch-set. Patch-set V14 is the latest version in that series. This series
   works in conjunction with RFC V4-rc1, present at the following link.

[*] ARM Architecture *Specific* Patch-set
   RFC V3 [4]: https://github.com/salil-mehta/qemu.git virt-cpuhp-armv8/rfc-v3
   RFC V4-rc1: https://github.com/salil-mehta/qemu.git virt-cpuhp-armv8/rfc-v4-rc1 (combined)


References:

[1] https://lore.kernel.org/qemu-devel/20230926100436.28284-1-salil.mehta@huawei.com/
[2] https://lore.kernel.org/all/20230913163823.7880-1-james.morse@arm.com/
[3] https://lore.kernel.org/qemu-devel/cover.1695697701.git.lixianglai@loongson.cn/
[4] https://lore.kernel.org/qemu-devel/20240613233639.202896-2-salil.mehta@huawei.com/



