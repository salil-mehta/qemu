Subject: [PATCH RFC V3 00/29] Support of Virtual CPU Hotplug for ARMv8 Arch
Date: Fri, 14 Jun 2024 00:36:10 +0100	[thread overview]
Message-ID: <20240613233639.202896-1-salil.mehta@huawei.com> (raw)

PROLOGUE
========

To assist in review and set the right expectations from this RFC, please first
read the sections *APPENDED AT THE END* of this cover letter:

1. Important *DISCLAIMER* [Section (X)]
2. Work presented at KVMForum Conference (slides available) [Section (V)F]
3. Organization of patches [Section (XI)]
4. References [Section (XII)]
5. Detailed TODO list of leftover work or work-in-progress [Section (IX)]

There has been interest shown by other organizations in adapting this series
for their architecture. Hence, RFC V2 [21] has been split into architecture
*agnostic* [22] and *specific* patch sets.

This is an ARM architecture-specific patch set carved out of RFC V2. Please
check section (XI)B for details of architecture agnostic patches.

SECTIONS [I - XIII] are as follows:

(I) Key Changes [details in last section (XIV)]
==============================================

RFC V2 -> RFC V3

1. Split into Architecture *agnostic* (V13) [22] and *specific* (RFC V3) patch sets.
2. Addressed comments by Gavin Shan (RedHat), Shaoqin Huang (RedHat), Philippe Mathieu-Daudé (Linaro),
   Jonathan Cameron (Huawei), Zhao Liu (Intel).

RFC V1 -> RFC V2

RFC V1: https://lore.kernel.org/qemu-devel/20200613213629.21984-1-salil.mehta@huawei.com/

1. ACPI MADT Table GIC CPU Interface can now be presented [6] as ACPI
   *online-capable* or *enabled* to the Guest OS at boot time. This means
   associated CPUs can have ACPI _STA as *enabled* or *disabled* even after boot.
   See UEFI ACPI 6.5 Spec, Section 05, Table 5.37 GICC CPU Interface Flags[20].
2. SMCC/HVC Hypercall exit handling in userspace/Qemu for PSCI CPU_{ON,OFF}
   request. This is required to {dis}allow online'ing a vCPU.
3. Always presenting unplugged vCPUs in CPUs ACPI AML code as ACPI _STA.PRESENT 
   to the Guest OS. Toggling ACPI _STA.Enabled to give an effect of the
   hot{un}plug.
4. Live Migration works (some issues are still there).
5. TCG/HVF/qtest does not support Hotplug and falls back to default.
6. Code for TCG support exists in this release (it is a work-in-progress).
7. ACPI _OSC method can now be used by OSPM to negotiate Qemu VM platform
   hotplug capability (_OSC Query support still pending).
8. Misc. Bug fixes.

(II) Summary
============

This patch set introduces virtual CPU hotplug support for the ARMv8 architecture
in QEMU. The idea is to be able to hotplug and hot-unplug vCPUs while the guest VM
is running, without requiring a reboot. This does *not* make any assumptions about
the physical CPU hotplug availability within the host system but rather tries to
solve the problem at the virtualizer/QEMU layer. It introduces ACPI CPU hotplug hooks
and event handling to interface with the guest kernel, and code to initialize, plug,
and unplug CPUs. No changes are required within the host kernel/KVM except the
support of hypercall exit handling in the user-space/Qemu, which has recently
been added to the kernel. Corresponding guest kernel changes have been
posted on the mailing list [3] [4] by James Morse.

(III) Motivation
================

This allows scaling the guest VM compute capacity on-demand, which would be
useful for the following example scenarios:

1. Vertical Pod Autoscaling [9][10] in the cloud: Part of the orchestration
   framework that could adjust resource requests (CPU and Mem requests) for
   the containers in a pod, based on usage.
2. Pay-as-you-grow Business Model: Infrastructure providers could allocate and
   restrict the total number of compute resources available to the guest VM
   according to the SLA (Service Level Agreement). VM owners could request more
   compute to be hot-plugged for some cost.

For example, Kata Container VM starts with a minimum amount of resources (i.e.,
hotplug everything approach). Why?

1. Allowing faster *boot time* and
2. Reduction in *memory footprint*

Kata Container VM can boot with just 1 vCPU, and then later more vCPUs can be
hot-plugged as needed.

(IV) Terminology
================

(*) Possible CPUs: Total vCPUs that could ever exist in the VM. This includes
                   any cold-booted CPUs plus any CPUs that could be later
                   hot-plugged.
                   - Qemu parameter (-smp maxcpus=N)
(*) Present CPUs:  Possible CPUs that are ACPI 'present'. These might or might
                   not be ACPI 'enabled'. 
                   - Present vCPUs = Possible vCPUs (Always on ARM Arch)
(*) Enabled CPUs:  Possible CPUs that are ACPI 'present' and 'enabled' and can
                   now be ‘onlined’ (PSCI) for use by the Guest Kernel. All cold-
                   booted vCPUs are ACPI 'enabled' at boot. Later, using
                   device_add, more vCPUs can be hotplugged and made ACPI
                   'enabled'.
                   - Qemu parameter (-smp cpus=N). Can be used to specify some
	           cold-booted vCPUs during VM init. Some can be added using the
	           '-device' option.

(V) Constraints Due to ARMv8 CPU Architecture [+] Other Impediments
===================================================================

A. Physical Limitation to Support CPU Hotplug: (Architectural Constraint)
   1. ARMv8 CPU architecture does not support the concept of the physical CPU
      hotplug. 
      a. There are many per-CPU components like PMU, SVE, MTE, Arch timers, etc.,
         whose behavior needs to be clearly defined when the CPU is hot(un)plugged.
         There is no specification for this.

   2. Other ARM components like GIC, etc., have not been designed to realize
      physical CPU hotplug capability as of now. For example,
      a. Every physical CPU has a unique GICC (GIC CPU Interface) by construct.
         Architecture does not specify what CPU hot(un)plug would mean in
         context to any of these.
      b. CPUs/GICC are physically connected to unique GICR (GIC Redistributor).
         GIC Redistributors are always part of the always-on power domain. Hence,
         they cannot be powered off as per specification.

B. Impediments in Firmware/ACPI (Architectural Constraint)

   1. Firmware has to expose GICC, GICR, and other per-CPU features like PMU,
      SVE, MTE, Arch Timers, etc., to the OS. Due to the architectural constraint
      stated in section A1(a), all interrupt controller structures of
      MADT describing GIC CPU Interfaces and the GIC Redistributors MUST be
      presented by firmware to the OSPM during boot time.
   2. Architectures that support CPU hotplug can evaluate the ACPI _MAT method to
      get this kind of information from the firmware even after boot, and the
      OSPM has the capability to process these. ARM kernel uses information in MADT
      interrupt controller structures to identify the number of present CPUs during
      boot and hence does not allow to change these after boot. The number of
      present CPUs cannot be changed. It is an architectural constraint!

C. Impediments in KVM to Support Virtual CPU Hotplug (Architectural Constraint)

   1. KVM VGIC:
      a. Sizing of various VGIC resources like memory regions, etc., related to
         the redistributor happens only once and is fixed at the VM init time
         and cannot be changed later after initialization has happened.
         KVM statically configures these resources based on the number of vCPUs
         and the number/size of redistributor ranges.
      b. Association between vCPU and its VGIC redistributor is fixed at the
         VM init time within the KVM, i.e., when redistributor iodevs gets
         registered. VGIC does not allow to setup/change this association
         after VM initialization has happened. Physically, every CPU/GICC is
         uniquely connected with its redistributor, and there is no
         architectural way to set this up.
   2. KVM vCPUs:
      a. Lack of specification means destruction of KVM vCPUs does not exist as
         there is no reference to tell what to do with other per-vCPU
         components like redistributors, arch timer, etc.
      b. In fact, KVM does not implement the destruction of vCPUs for any
         architecture. This is independent of whether the architecture
         actually supports CPU Hotplug feature. For example, even for x86 KVM
         does not implement the destruction of vCPUs.

D. Impediments in Qemu to Support Virtual CPU Hotplug (KVM Constraints->Arch)

   1. Qemu CPU Objects MUST be created to initialize all the Host KVM vCPUs to
      overcome the KVM constraint. KVM vCPUs are created and initialized when Qemu
      CPU Objects are realized. But keeping the QOM CPU objects realized for
      'yet-to-be-plugged' vCPUs can create problems when these new vCPUs shall
      be plugged using device_add and a new QOM CPU object shall be created.
   2. GICV3State and GICV3CPUState objects MUST be sized over *possible vCPUs*
      during VM init time while QOM GICV3 Object is realized. This is because
      KVM VGIC can only be initialized once during init time. But every
      GICV3CPUState has an associated QOM CPU Object. Later might correspond to
      vCPU which are 'yet-to-be-plugged' (unplugged at init).
   3. How should new QOM CPU objects be connected back to the GICV3CPUState
      objects and disconnected from it in case the CPU is being hot(un)plugged?
   4. How should 'unplugged' or 'yet-to-be-plugged' vCPUs be represented in the
      QOM for which KVM vCPU already exists? For example, whether to keep,
       a. No QOM CPU objects Or
       b. Unrealized CPU Objects
   5. How should vCPU state be exposed via ACPI to the Guest? Especially for
      the unplugged/yet-to-be-plugged vCPUs whose CPU objects might not exist
      within the QOM but the Guest always expects all possible vCPUs to be
      identified as ACPI *present* during boot.
   6. How should Qemu expose GIC CPU interfaces for the unplugged or
      yet-to-be-plugged vCPUs using ACPI MADT Table to the Guest?

E. Summary of Approach ([+] Workarounds to problems in sections A, B, C & D)

   1. At VM Init, pre-create all the possible vCPUs in the Host KVM i.e., even
      for the vCPUs which are yet-to-be-plugged in Qemu but keep them in the
      powered-off state.
   2. After the KVM vCPUs have been initialized in the Host, the KVM vCPU
      objects corresponding to the unplugged/yet-to-be-plugged vCPUs are parked
      at the existing per-VM "kvm_parked_vcpus" list in Qemu. (similar to x86)
   3. GICV3State and GICV3CPUState objects are sized over possible vCPUs during
      VM init time i.e., when Qemu GIC is realized. This, in turn, sizes KVM VGIC
      resources like memory regions, etc., related to the redistributors with the
      number of possible KVM vCPUs. This never changes after VM has initialized.
   4. Qemu CPU objects corresponding to unplugged/yet-to-be-plugged vCPUs are
      released post Host KVM CPU and GIC/VGIC initialization.
   5. Build ACPI MADT Table with the following updates:
      a. Number of GIC CPU interface entries (=possible vCPUs)
      b. Present Boot vCPU as MADT.GICC.Enabled=1 (Not hot[un]pluggable) 
      c. Present hot(un)pluggable vCPUs as MADT.GICC.online-capable=1  
         - MADT.GICC.Enabled=0 (Mutually exclusive) [6][7]
	 - vCPU can be ACPI enabled+onlined after Guest boots (Firmware Policy) 
	 - Some issues with above (details in later sections)
   6. Expose below ACPI Status to Guest kernel:
      a. Always _STA.Present=1 (all possible vCPUs)
      b. _STA.Enabled=1 (plugged vCPUs)
      c. _STA.Enabled=0 (unplugged vCPUs)
   7. vCPU hotplug *realizes* new QOM CPU object. The following happens:
      a. Realizes, initializes QOM CPU Object & spawns Qemu vCPU thread.
      b. Unparks the existing KVM vCPU ("kvm_parked_vcpus" list).
         - Attaches to QOM CPU object.
      c. Reinitializes KVM vCPU in the Host.
         - Resets the core and sys regs, sets defaults, etc.
      d. Runs KVM vCPU (created with "start-powered-off").
	 - vCPU thread sleeps (waits for vCPU reset via PSCI). 
      e. Updates Qemu GIC.
         - Wires back IRQs related to this vCPU.
         - GICV3CPUState association with QOM CPU Object.
      f. Updates [6] ACPI _STA.Enabled=1.
      g. Notifies Guest about the new vCPU (via ACPI GED interface).
	 - Guest checks _STA.Enabled=1.
	 - Guest adds processor (registers CPU with LDM) [3].
      h. Plugs the QOM CPU object in the slot.
         - slot-number = cpu-index {socket, cluster, core, thread}.
      i. Guest online's vCPU (CPU_ON PSCI call over HVC/SMC).
         - KVM exits HVC/SMC Hypercall [5] to Qemu (Policy Check).
         - Qemu powers-on KVM vCPU in the Host.
   8. vCPU hot-unplug *unrealizes* QOM CPU Object. The following happens:
      a. Notifies Guest (via ACPI GED interface) vCPU hot-unplug event.
         - Guest offline's vCPU (CPU_OFF PSCI call over HVC/SMC).
      b. KVM exits HVC/SMC Hypercall [5] to Qemu (Policy Check).
         - Qemu powers-off the KVM vCPU in the Host.
      c. Guest signals *Eject* vCPU to Qemu.
      d. Qemu updates [6] ACPI _STA.Enabled=0.
      e. Updates GIC.
         - Un-wires IRQs related to this vCPU.
         - GICV3CPUState association with new QOM CPU Object is updated.
      f. Unplugs the vCPU.
	 - Removes from slot.
         - Parks KVM vCPU ("kvm_parked_vcpus" list).
         - Unrealizes QOM CPU Object & joins back Qemu vCPU thread.
	 - Destroys QOM CPU object.
      g. Guest checks ACPI _STA.Enabled=0.
         - Removes processor (unregisters CPU with LDM) [3].

F. Work Presented at KVM Forum Conferences:
==========================================

Details of the above work have been presented at KVMForum2020 and KVMForum2023
conferences. Slides & video are available at the links below:
a. KVMForum 2023
   - Challenges Revisited in Supporting Virt CPU Hotplug on architectures that don't Support CPU Hotplug (like ARM64).
     https://kvm-forum.qemu.org/2023/KVM-forum-cpu-hotplug_7OJ1YyJ.pdf
     https://kvm-forum.qemu.org/2023/Challenges_Revisited_in_Supporting_Virt_CPU_Hotplug_-__ii0iNb3.pdf
     https://www.youtube.com/watch?v=hyrw4j2D6I0&t=23970s
     https://kvm-forum.qemu.org/2023/talk/9SMPDQ/
b. KVMForum 2020
   - Challenges in Supporting Virtual CPU Hotplug on SoC Based Systems (like ARM64) - Salil Mehta, Huawei.
     https://sched.co/eE4m

(VI) Commands Used
==================

A. Qemu launch commands to init the machine:

    $ qemu-system-aarch64 --enable-kvm -machine virt,gic-version=3 \
      -cpu host -smp cpus=4,maxcpus=6 \
      -m 300M \
      -kernel Image \
      -initrd rootfs.cpio.gz \
      -append "console=ttyAMA0 root=/dev/ram rdinit=/init maxcpus=2 acpi=force" \
      -nographic \
      -bios QEMU_EFI.fd \

B. Hot-(un)plug related commands:

  # Hotplug a host vCPU (accel=kvm):
    $ device_add host-arm-cpu,id=core4,core-id=4

  # Hotplug a vCPU (accel=tcg):
    $ device_add cortex-a57-arm-cpu,id=core4,core-id=4

  # Delete the vCPU:
    $ device_del core4

Sample output on guest after boot:

    $ cat /sys/devices/system/cpu/possible
    0-5
    $ cat /sys/devices/system/cpu/present
    0-5
    $ cat /sys/devices/system/cpu/enabled
    0-3
    $ cat /sys/devices/system/cpu/online
    0-1
    $ cat /sys/devices/system/cpu/offline
    2-5

Sample output on guest after hotplug of vCPU=4:

    $ cat /sys/devices/system/cpu/possible
    0-5
    $ cat /sys/devices/system/cpu/present
    0-5
    $ cat /sys/devices/system/cpu/enabled
    0-4
    $ cat /sys/devices/system/cpu/online
    0-1,4
    $ cat /sys/devices/system/cpu/offline
    2-3,5

    Note: vCPU=4 was explicitly 'onlined' after hot-plug
    $ echo 1 > /sys/devices/system/cpu/cpu4/online

(VII) Latest Repository
=======================

(*) Latest Qemu RFC V3 (Architecture Specific) patch set:
    https://github.com/salil-mehta/qemu.git virt-cpuhp-armv8/rfc-v3
(*) Latest Qemu V13 (Architecture Agnostic) patch set:
    https://github.com/salil-mehta/qemu.git virt-cpuhp-armv8/rfc-v3.arch.agnostic.v13
(*) QEMU changes for vCPU hotplug can be cloned from below site:
    https://github.com/salil-mehta/qemu.git virt-cpuhp-armv8/rfc-v2
(*) Guest Kernel changes (by James Morse, ARM) are available here:
    https://git.kernel.org/pub/scm/linux/kernel/git/morse/linux.git virtual_cpu_hotplug/rfc/v2
(*) Leftover patches of the kernel are available here:
    https://lore.kernel.org/lkml/20240529133446.28446-1-Jonathan.Cameron@huawei.com/
    https://github.com/salil-mehta/linux/commits/virtual_cpu_hotplug/rfc/v6.jic/ (not latest)

(VIII) KNOWN ISSUES
===================

1. Migration has been lightly tested but has been found working.
2. TCG is broken.
3. HVF and qtest are not supported yet.
4. ACPI MADT Table flags [7] MADT.GICC.Enabled and MADT.GICC.online-capable are
   mutually exclusive, i.e., as per the change [6], a vCPU cannot be both
   GICC.Enabled and GICC.online-capable. This means:
      [ Link: https://bugzilla.tianocore.org/show_bug.cgi?id=3706 ]
   a. If we have to support hot-unplug of the cold-booted vCPUs, then these MUST
      be specified as GICC.online-capable in the MADT Table during boot by the
      firmware/Qemu. But this requirement conflicts with the requirement to
      support new Qemu changes with legacy OS that don't understand
      MADT.GICC.online-capable Bit. Legacy OS during boot time will ignore this
      bit, and hence these vCPUs will not appear on such OS. This is unexpected
      behavior.
   b. In case we decide to specify vCPUs as MADT.GICC.Enabled and try to unplug
      these cold-booted vCPUs from OS (which in actuality should be blocked by
      returning error at Qemu), then features like 'kexec' will break.
   c. As I understand, removal of the cold-booted vCPUs is a required feature
      and x86 world allows it.
   d. Hence, either we need a specification change to make the MADT.GICC.Enabled
      and MADT.GICC.online-capable Bits NOT mutually exclusive or NOT support
      the removal of cold-booted vCPUs. In the latter case, a check can be introduced
      to bar the users from unplugging vCPUs, which were cold-booted, using QMP
      commands. (Needs discussion!)
      Please check the patch part of this patch set:
      [hw/arm/virt: Expose cold-booted CPUs as MADT GICC Enabled].
   
      NOTE: This is definitely not a blocker!
5. Code related to the notification to GICV3 about the hot(un)plug of a vCPU event
   might need further discussion.


(IX) THINGS TO DO
=================

1. Fix issues related to TCG/Emulation support. (Not a blocker)
2. Comprehensive Testing is in progress. (Positive feedback from Oracle & Ampere)
3. Qemu Documentation (.rst) needs to be updated.
4. Fix qtest, HVF Support (Future).
5. Update the design issue related to ACPI MADT.GICC flags discussed in known
   issues. This might require UEFI ACPI specification change (Not a blocker).
6. Add ACPI _OSC 'Query' support. Only part of _OSC support exists now. (Not a blocker).

The above is *not* a complete list. Will update later!

Best regards,  
Salil.

(X) DISCLAIMER
==============

This work is an attempt to present a proof-of-concept of the ARM64 vCPU hotplug
implementation to the community. This is *not* production-level code and might
have bugs. Comprehensive testing is being done on HiSilicon Kunpeng920 SoC,
Oracle, and Ampere servers. We are nearing stable code and a non-RFC
version shall be floated soon.

This work is *mostly* in the lines of the discussions that have happened in the
previous years [see refs below] across different channels like the mailing list,
Linaro Open Discussions platform, and various conferences like KVMForum, etc. This
RFC is being used as a way to verify the idea mentioned in this cover letter and
to get community views. Once this has been agreed upon, a formal patch shall be
posted to the mailing list for review.

[The concept being presented has been found to work!]

(XI) ORGANIZATION OF PATCHES
============================
 
A. Architecture *specific* patches:

   [Patch 1-8, 17, 27, 29] logic required during machine init.
    (*) Some validation checks.
    (*) Introduces core-id property and some util functions required later.
    (*) Logic to pre-create vCPUs.
    (*) GIC initialization pre-sized with possible vCPUs.
    (*) Some refactoring to have common hot and cold plug logic together.
    (*) Release of disabled QOM CPU objects in post_cpu_init().
    (*) Support of ACPI _OSC method to negotiate platform hotplug capabilities.
   [Patch 9-16] logic related to ACPI at machine init time.
    (*) Changes required to Enable ACPI for CPU hotplug.
    (*) Initialization of ACPI GED framework to cater to CPU Hotplug Events.
    (*) ACPI MADT/MAT changes.
   [Patch 18-26] logic required during vCPU hot-(un)plug.
    (*) Basic framework changes to support vCPU hot-(un)plug.
    (*) ACPI GED changes for hot-(un)plug hooks.
    (*) Wire-unwire the IRQs.
    (*) GIC notification logic.
    (*) ARMCPU unrealize logic.
    (*) Handling of SMCC Hypercall Exits by KVM to Qemu.
   
B. Architecture *agnostic* patches:

   [PATCH V13 0/8] Add architecture agnostic code to support vCPU Hotplug.
   https://lore.kernel.org/qemu-devel/20240607115649.214622-1-salil.mehta@huawei.com/T/#md0887eb07976bc76606a8204614ccc7d9a01c1f7
    (*) Refactors vCPU create, Parking, unparking logic of vCPUs, and addition of traces.
    (*) Build ACPI AML related to CPU control dev.
    (*) Changes related to the destruction of CPU Address Space.
    (*) Changes related to the uninitialization of GDB Stub.
    (*) Updating of Docs.

(XII) REFERENCES
================

[1] https://lore.kernel.org/qemu-devel/20200613213629.21984-1-salil.mehta@huawei.com/
[2] https://lore.kernel.org/linux-arm-kernel/20200625133757.22332-1-salil.mehta@huawei.com/
[3] https://lore.kernel.org/lkml/20230203135043.409192-1-james.morse@arm.com/
[4] https://lore.kernel.org/all/20230913163823.7880-1-james.morse@arm.com/
[5] https://lore.kernel.org/all/20230404154050.2270077-1-oliver.upton@linux.dev/
[6] https://bugzilla.tianocore.org/show_bug.cgi?id=3706
[7] https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#gic-cpu-interface-gicc-structure
[8] https://bugzilla.tianocore.org/show_bug.cgi?id=4481#c5
[9] https://cloud.google.com/kubernetes-engine/docs/concepts/verticalpodautoscaler
[10] https://docs.aws.amazon.com/eks/latest/userguide/vertical-pod-autoscaler.html
[11] https://lkml.org/lkml/2019/7/10/235
[12] https://lists.cs.columbia.edu/pipermail/kvmarm/2018-July/032316.html
[13] https://lists.gnu.org/archive/html/qemu-devel/2020-01/msg06517.html
[14] https://op-lists.linaro.org/archives/list/linaro-open-discussions@op-lists.linaro.org/thread/7CGL6JTACPUZEYQC34CZ2ZBWJGSR74WE/
[15] http://lists.nongnu.org/archive/html/qemu-devel/2018-07/msg01168.html
[16] https://lists.gnu.org/archive/html/qemu-devel/2020-06/msg00131.html
[17] https://op-lists.linaro.org/archives/list/linaro-open-discussions@op-lists.linaro.org/message/X74JS6P2N4AUWHHATJJVVFDI2EMDZJ74/
[18] https://lore.kernel.org/lkml/20210608154805.216869-1-jean-philippe@linaro.org/
[19] https://lore.kernel.org/all/20230913163823.7880-1-james.morse@arm.com/ 
[20] https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#gicc-cpu-interface-flags
[21] https://lore.kernel.org/qemu-devel/20230926100436.28284-1-salil.mehta@huawei.com/
[22] https://lore.kernel.org/qemu-devel/20240607115649.214622-1-salil.mehta@huawei.com/T/#md0887eb07976bc76606a8204614ccc7d9a01c1f7

(XIII) ACKNOWLEDGEMENTS
=======================

I would like to take this opportunity to thank below people for various
discussions with me over different channels during the development:

Marc Zyngier (Google)               Catalin Marinas (ARM),         
James Morse(ARM),                   Will Deacon (Google), 
Jean-Phillipe Brucker (Linaro),     Sudeep Holla (ARM),
Lorenzo Pieralisi (Linaro),         Gavin Shan (Redhat), 
Jonathan Cameron (Huawei),          Darren Hart (Ampere),
Igor Mamedov (Redhat),              Ilkka Koskinen (Ampere),
Andrew Jones (Redhat),              Karl Heubaum (Oracle),
Keqian Zhu (Huawei),                Miguel Luis (Oracle),
Xiongfeng Wang (Huawei),            Vishnu Pajjuri (Ampere),
Shameerali Kolothum (Huawei)        Russell King (Oracle)
Xuwei/Joy (Huawei),                 Peter Maydel (Linaro)
Zengtao/Prime (Huawei),             And all those whom I have missed! 

Many thanks to the following people for their current or past contributions:

1. James Morse (ARM)
   (Current Kernel part of vCPU Hotplug Support on AARCH64)
2. Jean-Philippe Brucker (Linaro)
   (Prototyped one of the earlier PSCI-based POC [17][18] based on RFC V1)
3. Keqian Zhu (Huawei)
   (Co-developed Qemu prototype)
4. Xiongfeng Wang (Huawei)
   (Co-developed an earlier kernel prototype with me)
5. Vishnu Pajjuri (Ampere)
   (Verification on Ampere ARM64 Platforms + fixes)
6. Miguel Luis (Oracle)
   (Verification on Oracle ARM64 Platforms + fixes)
7. Russell King (Oracle) & Jonathan Cameron (Huawei)
   (Helping in upstreaming James Morse's Kernel patches).

(XIV) Change Log:
=================

RFC V2 -> RFC V3:
-----------------
1. Miscellaneous:
   - Split the RFC V2 into arch-agnostic and arch-specific patch sets.
2. Addressed Gavin Shan's (RedHat) comments:
   - Made CPU property accessors inline.
     https://lore.kernel.org/qemu-devel/6cd28639-2cfa-f233-c6d9-d5d2ec5b1c58@redhat.com/
   - Collected Reviewed-bys [PATCH RFC V2 4/37, 14/37, 22/37].
   - Dropped the patch as it was not required after init logic was refactored.
     https://lore.kernel.org/qemu-devel/4fb2eef9-6742-1eeb-721a-b3db04b1be97@redhat.com/
   - Fixed the range check for the core during vCPU Plug.
     https://lore.kernel.org/qemu-devel/1c5fa24c-6bf3-750f-4f22-087e4a9311af@redhat.com/
   - Added has_hotpluggable_vcpus check to make build_cpus_aml() conditional.
     https://lore.kernel.org/qemu-devel/832342cb-74bc-58dd-c5d7-6f995baeb0f2@redhat.com/
   - Fixed the states initialization in cpu_hotplug_hw_init() to accommodate previous refactoring.
     https://lore.kernel.org/qemu-devel/da5e5609-1883-8650-c7d8-6868c7b74f1c@redhat.com/
   - Fixed typos.
     https://lore.kernel.org/qemu-devel/eb1ac571-7844-55e6-15e7-3dd7df21366b@redhat.com/
   - Removed the unnecessary 'goto fail'.
     https://lore.kernel.org/qemu-devel/4d8980ac-f402-60d4-fe52-787815af8a7d@redhat.com/#t
   - Added check for hotpluggable vCPUs in the _OSC method.
     https://lore.kernel.org/qemu-devel/20231017001326.FUBqQ1PTowF2GxQpnL3kIW0AhmSqbspazwixAHVSi6c@z/
3. Addressed Shaoqin Huang's (Intel) comments:
   - Fixed the compilation break due to the absence of a call to virt_cpu_properties() missing
     along with its definition.
     https://lore.kernel.org/qemu-devel/3632ee24-47f7-ae68-8790-26eb2cf9950b@redhat.com/
4. Addressed Jonathan Cameron's (Huawei) comments:
   - Gated the 'disabled vcpu message' for GIC version < 3.
     https://lore.kernel.org/qemu-devel/20240116155911.00004fe1@Huawei.com/

RFC V1 -> RFC V2:
-----------------
1. Addressed James Morse's (ARM) requirement as per Linaro Open Discussion:
   - Exposed all possible vCPUs as always ACPI _STA.present and available during boot time.
   - Added the _OSC handling as required by James's patches.
   - Introduction of 'online-capable' bit handling in the flag of MADT GICC.
   - SMCC Hypercall Exit handling in Qemu.
2. Addressed Marc Zyngier's comment:
   - Fixed the note about GIC CPU Interface in the cover letter.
3. Addressed issues raised by Vishnu Pajjuru (Ampere) & Miguel Luis (Oracle) during testing:
   - Live/Pseudo Migration crashes.
4. Others:
   - Introduced the concept of persistent vCPU at QOM.
   - Introduced wrapper APIs of present, possible, and persistent.
   - Change at ACPI hotplug H/W init leg accommodating initializing is_present and is_enabled states.
   - Check to avoid unplugging cold-booted vCPUs.
   - Disabled hotplugging with TCG/HVF/QTEST.
   - Introduced CPU Topology, {socket, cluster, core, thread}-id property.
   - Extract virt CPU properties as a common virt_vcpu_properties() function.




















Documentation
=============

Documentation can be found hosted online at
`<https://www.qemu.org/documentation/>`_. The documentation for the
current development version that is available at
`<https://www.qemu.org/docs/master/>`_ is generated from the ``docs/``
folder in the source tree, and is built by `Sphinx
<https://www.sphinx-doc.org/en/master/>`_.


Building
========

QEMU is multi-platform software intended to be buildable on all modern
Linux platforms, OS-X, Win32 (via the Mingw64 toolchain) and a variety
of other UNIX targets. The simple steps to build QEMU are:


.. code-block:: shell

  mkdir build
  cd build
  ../configure
  make

Additional information can also be found online via the QEMU website:

* `<https://wiki.qemu.org/Hosts/Linux>`_
* `<https://wiki.qemu.org/Hosts/Mac>`_
* `<https://wiki.qemu.org/Hosts/W32>`_


Submitting patches
==================

The QEMU source code is maintained under the GIT version control system.

.. code-block:: shell

   git clone https://gitlab.com/qemu-project/qemu.git

When submitting patches, one common approach is to use 'git
format-patch' and/or 'git send-email' to format & send the mail to the
qemu-devel@nongnu.org mailing list. All patches submitted must contain
a 'Signed-off-by' line from the author. Patches should follow the
guidelines set out in the `style section
<https://www.qemu.org/docs/master/devel/style.html>`_ of
the Developers Guide.

Additional information on submitting patches can be found online via
the QEMU website

* `<https://wiki.qemu.org/Contribute/SubmitAPatch>`_
* `<https://wiki.qemu.org/Contribute/TrivialPatches>`_

The QEMU website is also maintained under source control.

.. code-block:: shell

  git clone https://gitlab.com/qemu-project/qemu-web.git

* `<https://www.qemu.org/2017/02/04/the-new-qemu-website-is-up/>`_

* `<mailto:qemu-devel@nongnu.org>`_
* `<https://lists.nongnu.org/mailman/listinfo/qemu-devel>`_
* #qemu on irc.oftc.net

Information on additional methods of contacting the community can be
found online via the QEMU website:

* `<https://wiki.qemu.org/Contribute/StartHere>`_
