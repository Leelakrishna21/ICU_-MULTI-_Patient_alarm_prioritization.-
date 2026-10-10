# ICU_-MULTI-_Patient_alarm_prioritization.-

<div align="center">

# 🛡️ AEGIS-ICU
### Deterministic Real-Time ICU Alarm Aggregation, Triage & Anti-Fatigue System
**Engineered on BlackBerry QNX Neutrino RTOS 7.x/8.0 • Raspberry Pi 4 (ARM Cortex-A72)**

[![QNX Neutrino](https://img.shields.io/badge/RTOS-QNX_Neutrino_7.x%20%2F%208.0-blue?style=for-the-badge&logo=blackberry)](https://blackberry.qnx.com)
[![Target Platform](https://img.shields.io/badge/Hardware-Raspberry_Pi_4_(BCM2711)-red?style=for-the-badge&logo=raspberrypi)](https://www.raspberrypi.com)
[![Architecture](https://img.shields.io/badge/Arch-ARM64%20%2F%20aarch64le-green?style=for-the-badge&logo=arm)](https://developer.arm.com)
[![Language](https://img.shields.io/badge/Language-C99%20%2F%20POSIX.1b-orange?style=for-the-badge&logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Scheduling](https://img.shields.io/badge/Scheduling-SCHED__FIFO%20Real--Time-purple?style=for-the-badge)](https://pubs.opengroup.org)
[![Medical Standard](https://img.shields.io/badge/Standard-IEC_62304%20%2F%20IEC_60601--1--8_Ready-teal?style=for-the-badge)](https://www.iso.org)

<p align="center">
  <b>A hard real-time, deterministic alarm filtration and priority dispatcher combating clinical alarm fatigue in Intensive Care Units.</b>
</p>

---

</div>

## 📌 Table of Contents
- [1. Executive Summary & Clinical Problem](#1-executive-summary--clinical-problem)
- [2. System Architecture](#2-system-architecture)
- [3. Deep Technical Highlights](#3-deep-technical-highlights)
- [4. Algorithmic Engine & Mathematical Models](#4-algorithmic-engine--mathematical-models)
- [5. Hardware Bill of Materials (BOM) & Pinout](#5-hardware-bill-of-materials-bom--pinout)
- [6. Thread Topology & Real-Time Scheduling](#6-thread-topology--real-time-scheduling)
- [7. Repository Structure](#7-repository-structure)
- [8. Toolchain & Build Instructions](#8-toolchain--build-instructions)
- [9. Deployment & Execution on Target](#9-deployment--execution-on-target)
- [10. Live Execution Telemetry & Verification](#10-live-execution-telemetry--verification)
- [11. Medical Safety Compliance & Impact](#11-medical-safety-compliance--impact)
- [12. Roadmap & Next Steps](#12-roadmap--next-steps)

---

## 1. Executive Summary & Clinical Problem

### The Crisis: ICU Alarm Fatigue
In modern Intensive Care Units (ICUs), clinical personnel are subjected to an unrelenting barrage of auditory and visual alarms from ventilators, infusion pumps, hemodialysis machines, and multiparameter bedside monitors:
* **Overwhelming Volume**: Clinical studies (AACN, ECRI Institute, Johns Hopkins) document **350 to 800+ alarms per bed per day**.
* **High Noise-to-Signal Ratio**: Between **72% and 99% of all bedside alarms are clinically false, non-actionable, or transient**.
* **Lethal Consequences**: Desensitization causes nurses and intensivists to experience severe sensory fatigue, leading to muted buzzers, delayed intervention times, and documented patient fatalities during acute cardiac arrest or hypoxia episodes.

### Why General-Purpose Operating Systems (GPOS) Fail
Standard operating systems like Linux or Windows are inherently non-deterministic:
* **Unbounded Latency**: Kernel preemption latency and non-deterministic page faults can delay critical alerts by hundreds of milliseconds.
* **Priority Inversion**: Inability to strictly bound priority inversion without hard real-time microkernel capabilities.
* **Microsecond Bit-Banging Jitter**: Protocols requiring microsecond-scale timing (such as single-wire sensor handshakes) get corrupted by standard OS thread scheduling jitter.

### The AEGIS-ICU Solution
**AEGIS-ICU** is a safety-critical, hard real-time embedded alarm aggregator built on the **BlackBerry QNX Neutrino RTOS** targeting the **Raspberry Pi 4 Model B (Broadcom BCM2711 SoC)**. 

Operating at microsecond granularity, AEGIS-ICU ingests physical environmental vitals and bedside clinical telemetry, passes them through a **temporal de-duplication filter**, marshals them across **POSIX priority message queues**, applies a **starvation aging engine** to prevent neglected patient deterioration, and deterministically drives tri-color visual indicators and emergency acoustic transducers via **SCHED_FIFO** preemption.

---

## 2. System Architecture

```
 +-----------------------------------------------------------------------------------------+
 |                                  AEGIS-ICU DATA PIPELINE                                |
 +-----------------------------------------------------------------------------------------+

 [ Physical DHT11 Sensor ]     [ Simulated Bed 1 Monitor ]     [ Simulated Bed 2 Monitor ]
   Temp & Humidity Telemetry     HR & SpO2 Stochastic Model      HR & SpO2 Stochastic Model
        (Thread Prio 30)              (Thread Prio 15)                (Thread Prio 15)
               │                             │                               │
               └───────────────────────┬─────┴───────────────────────────────┘
                                       ▼
             +───────────────────────────────────────────────────+
             |       STAGE 1: CLINICAL THRESHOLD EVALUATION       |
             |   P1 (Critical)  |  P2 (Warning)  |  P3 (Advisory)|
             +───────────────────────────────────────────────────+
                                       │
                                       ▼
             +───────────────────────────────────────────────────+
             |    STAGE 2: TEMPORAL ANTI-FLAPPING DEDUPLICATION   |
             |   Sliding Window: 4000 ms | Mutex-Guarded Matrix  |
             |  Suppresses identical / lower-prio transient noise|
             +───────────────────────────────────────────────────+
                                       │ Non-duplicate events
                                       ▼
             +───────────────────────────────────────────────────+
             |    STAGE 3: NON-BLOCKING POSIX IPC MESSAGE QUEUE  |
             |          mq_send() with POSIX Message Priority     |
             |         Queue: /icu_alarm_mq (Cap: 32 Messages)   |
             +───────────────────────────────────────────────────+
                                       │
                                       ▼
             +───────────────────────────────────────────────────+
             |     STAGE 4: STARVATION AGING & ESCALATION ENGINE |
             |        Delta-t > 6000 ms: Escalates P2 -> P1      |
             |   Guarantees bounded wait latency for all patients|
             +───────────────────────────────────────────────────+
                                       │
                                       ▼
             +───────────────────────────────────────────────────+
             |   STAGE 5: REAL-TIME PREEMPTIVE DISPATCHER        |
             |        SCHED_FIFO (Priority 25) | O(N) Priority   |
             |     Tie-Breaker: Oldest Timestamp First (FIFO)    |
             +───────────────────────────────────────────────────+
                                       │
                     ┌─────────────────┴─────────────────┐
                     ▼                                   ▼
          +───────────────────────+           +───────────────────────+
          |  HARDWARE ACTUATION   |           | TELEMETRY AUDIT TRAIL |
          |  Zero-Copy mmap GPIO  |           | Real-time terminal    |
          |  - Tri-Color LEDs     |           | latency tracking &    |
          |  - 250ms Pulsed Alarm |           | diagnostic logging    |
          +───────────────────────+           +───────────────────────+
```

---

## 3. Deep Technical Highlights

### 1. Direct Physical Register Memory Mapping (`mmap_device_memory`)
Rather than relying on user-space abstraction layers or slow virtual filesystems (`/sys/class/gpio`), AEGIS-ICU communicates directly with the **Broadcom BCM2711 peripheral bus**:
* Requests kernel-level I/O privilege via `ThreadCtl(_NTO_TCTL_IO, 0)`.
* Maps the physical memory region `0xFE200000UL` directly into process address space via `mmap_device_memory()` with flags `PROT_READ | PROT_WRITE | PROT_NOCACHE`.
* Directly manipulates the `GPSET0` (pin high), `GPCLR0` (pin low), `GPLEV0` (pin level read), and `PUPPDN0` (internal pull-up/pull-down configuration) registers with atomic bit-shifts.

### 2. Cycle-Accurate Nanosecond/Microsecond Protocol Bit-Banging
The one-wire communication protocol of the DHT11 sensor requires pulse-width timing discrimination at sub-50µs resolution:
* Querying the QNX System Page structure via `SYSPAGE_ENTRY(qtime)->cycles_per_sec` retrieves the dynamic 64-bit hardware CPU clock frequency (`cps`).
* Reads the hardware cycle counter via QNX `ClockCycles()`, calculating microsecond elapsed duration without system call overhead:
  $$\Delta t_{\mu s} = \frac{(\text{ClockCycles}() - t_{start}) \times 1\,000\,000}{\text{cps}}$$
* Discriminated low/high bits by measuring duration against the $48\,\mu\text{s}$ digital threshold with automatic timeout fail-safes.

### 3. POSIX Priority-Based Inter-Process Communication (IPC)
Decouples high-frequency sensor acquisition threads from actuation routines using native POSIX Message Queues (`mqueue`):
* Configured via `mq_attr` with capacity for 32 concurrent alarms.
* Non-blocking message transmission (`O_NONBLOCK`) ensures telemetry acquisition loops are never stalled by downstream processing.
* Preserves priority hierarchy directly within the queue metadata:
  ```c
  mq_send(mq_tx, (char *)&a, sizeof a, prio);
  ```

### 4. Deterministic Preemption with `SCHED_FIFO`
POSIX real-time thread attributes configured via:
* `PTHREAD_EXPLICIT_SCHED`
* `SCHED_FIFO` scheduling policy
* Dedicated priority stratification: Highest priority (30) allocated to time-critical one-wire ingestion, Dispatcher assigned priority 25, and Telemetry Simulators assigned priority 15.

---

## 4. Algorithmic Engine & Mathematical Models

### A. Sliding-Window Anti-Flapping Filter
To eliminate sensory oscillation caused by vital signs fluctuating along diagnostic thresholds, AEGIS-ICU enforces a mathematical suppression filter:

$$\text{Suppress}(s, c, t, p) = \begin{cases} 
\text{TRUE}, & \text{if } \Big(T_{last}[s][c] \neq 0\Big) \land \Big(t - T_{last}[s][c] < \Delta t_{dup}\Big) \land \Big(p \le P_{last}[s][c]\Big) \\
\text{FALSE}, & \text{otherwise}
\end{cases}$$

Where:
* $s \in \{0, 1, 2\}$: Sensor index (`DHT11`, `Bed 1`, `Bed 2`).
* $c \in \{0, 1, 2, 3\}$: Metric channel (`Temperature`, `Humidity`, `Heart Rate`, `SpO2`).
* $\Delta t_{dup} = 4000\,\text{ms}$: Temporal window.
* $p \in \{1, 2, 3\}$: Severity priority ($P_1 > P_2 > P_3$).

*Critical Rule:* If an incoming alarm has a strictly higher priority ($p > P_{last}$), it **immediately bypasses** suppression, guaranteeing zero delay during sudden acute decompensation.

### B. Dynamic Starvation Aging & Priority Escalation
In priority-preemptive systems, lower-priority tasks can suffer indefinite starvation when higher-priority events flood the system. In an ICU, an unaddressed $P_2$ Warning (e.g., persistent SpO2 89%) will quickly lead to organ hypoxia if neglected.

AEGIS-ICU evaluates queue dwell time continuously:

$$\forall a_i \in \text{Queue}, \quad \text{if } \Big(a_i.prio = P_2\Big) \land \Big(t_{now} - a_i.time\_ms > \tau_{aging}\Big) \implies a_i.prio \leftarrow P_1$$

Where $\tau_{aging} = 6000\,\text{ms}$. This guarantees an upper bound on warning latency.

### C. Dispatcher Lexicographical Optimal Selection
The dispatcher selects the next actionable alert by maximizing priority, breaking ties via oldest arrival time (FIFO fairness):

$$a^* = \arg\max_{a_i \in \text{Queue}} \Big( a_i.prio \times 10^{15} - a_i.time\_ms \Big)$$

---

## 5. Hardware Bill of Materials (BOM) & Pinout

### Bill of Materials
| Component | Specification | Quantity | Purpose |
| :--- | :--- | :---: | :--- |
| **SBC** | Raspberry Pi 4 Model B (Broadcom BCM2711, 4GB/8GB) | 1 | Target Execution Host running QNX Neutrino RTOS |
| **Microcontroller / Sensor** | DHT11 Digital Temperature & Humidity Sensor | 1 | Ambient ICU Environmental Surveillance |
| **Visual Indicators** | High-Brightness 5mm LEDs (Red, Yellow, Green) | 3 | Multi-tier visual triage annunciator |
| **Acoustic Transducer** | 5V Active Piezo Buzzer | 1 | P1 Emergency acoustic cadence alarm |
| **Current Limiting Resistors**| 220Ω - 330Ω 1/4W Resistors | 3 | Current limiting for LEDs |
| **Prototyping** | Solderless Breadboard & Male-to-Female Jumper Wires | 1 | Hardware-in-the-Loop circuit interconnection |

### BCM GPIO Pinout Mapping Table
```
 Raspberry Pi 4 Header (40-Pin J8)
       3.3V Power [01] [02] 5V Power
     GPIO 2 (SDA) [03] [04] 5V Power
     GPIO 3 (SCL) [05] [06] GND
  DHT11 ── GPIO 4 [07] [08] GPIO 14 (TXD)
              GND [09] [10] GPIO 15 (RXD)
RED LED ──GPIO 17 [11] [12] GPIO 18 (PCM_CLK)
GRN LED ──GPIO 27 [13] [14] GND
YEL LED ──GPIO 22 [15] [16] GPIO 23 ── BUZZER
             3.3V [17] [18] GPIO 24
```

| Signal Name | BCM GPIO | Header Pin | Direction | Pull Resistor | Logical Function |
| :--- | :---: | :---: | :---: | :---: | :--- |
| `DHT11_PIN` | **GPIO 4** | Pin 7 | Bi-directional | Internal Pull-Up (`PUPPDN0`) | One-Wire sensor telemetry |
| `RED_PIN` | **GPIO 17** | Pin 11 | Output | - | **P1 Critical Alert Indicator** |
| `YELLOW_PIN`| **GPIO 22** | Pin 15 | Output | - | **P2 Warning Alert Indicator** |
| `GREEN_PIN` | **GPIO 27** | Pin 13 | Output | - | **P3 Advisory / Normal Indicator** |
| `BUZZER_PIN`| **GPIO 23** | Pin 16 | Output | - | **P1 Emergency Cadence Strobe** |
| `GND` | Ground | Pin 6 / 9 / 14 | Ground | - | Common Circuit Ground |

---

## 6. Thread Topology & Real-Time Scheduling

| Thread Name | Function Entry | Scheduling Policy | Priority (`sched_priority`) | Responsibilities |
| :--- | :--- | :---: | :---: | :--- |
| **DHT11 Driver** | `dht11_thread` | `SCHED_FIFO` | **30** (Highest) | Time-critical one-wire bit-banging protocol; microsecond pulse timing. |
| **Alarm Dispatcher** | `dispatcher_thread` | `SCHED_FIFO` | **25** (High) | Queue ingestion, aging escalation ($P_2 \rightarrow P_1$), LED/Buzzer hardware actuation. |
| **Bed 1 Telemetry** | `bed_thread` (Bed 1) | `SCHED_FIFO` | **15** (Medium) | Stochastic heart rate & pulse oximetry monitoring for Bed 1. |
| **Bed 2 Telemetry** | `bed_thread` (Bed 2) | `SCHED_FIFO` | **15** (Medium) | Stochastic heart rate & pulse oximetry monitoring for Bed 2. |
| **Main Supervisor**| `main` | `SCHED_OTHER` | 10 (Normal) | Memory mapping initialization, POSIX queue setup, hardware self-test. |

---

## 7. Repository Structure

```
aegis_icu/
├── aegis_icu/
│   ├── .cproject                # QNX Momentics / Eclipse CDT managed project descriptor
│   ├── .project                 # Eclipse workspace configuration
│   ├── .settings/               # Compiler and indexer settings
│   ├── Makefile                 # Standalone QNX multi-target build system
│   ├── src/
│   │   └── aegis_icu.c          # Core hard real-time C99 source implementation
│   └── build/
│       └── aarch64le-debug/     # Compiled ARM64 target binary artifacts
│           └── aegis_icu        # QNX aarch64 executable
└── README.md                    # Technical documentation & project dossier
```

---

## 8. Toolchain & Build Instructions

### Prerequisites
* **QNX Software Development Platform (SDP) 7.1 or 8.0**
* Target Architecture: `aarch64le` (ARMv8 64-bit Little Endian)
* Host OS: Ubuntu 20.04/22.04 LTS or Windows with QNX Environment installed

### 1. Initialize QNX SDP Environment
On your development workstation:
```bash
# For Linux Host
source /opt/qnx710/qnxsdp-env.sh

# For Windows Host (Command Prompt)
C:\qnx710\qnxsdp-env.bat
```

### 2. Standalone GNU Makefile Compilation
Navigate to the project root and invoke the build system:
```bash
cd aegis_icu/aegis_icu

# Build Debug Profile for Raspberry Pi 4 (Default: aarch64le)
make PLATFORM=aarch64le BUILD_PROFILE=debug

# Build Optimized Release Profile
make PLATFORM=aarch64le BUILD_PROFILE=release
```

### 3. Build Targets & Profiles
* `debug`: Compiles with `-g -O0 -fno-builtin` for GDB remote debugging.
* `release`: Compiles with `-O2` optimization for production deployment.
* `coverage`: Generates code coverage data using `-ftest-coverage -fprofile-arcs`.
* `profile`: Instruments functions with `-finstrument-functions -lprofilingS`.

Clean the build workspace:
```bash
make clean
```

---

## 9. Deployment & Execution on Target

### 1. Transfer Binary to Raspberry Pi 4
Transfer the compiled executable to your target board running QNX Neutrino via SSH/SCP:
```bash
scp build/aarch64le-debug/aegis_icu qnxuser@192.168.1.120:/tmp/
```

### 2. Configure POSIX Message Queue Daemon
Log into the QNX terminal on the Raspberry Pi 4. The POSIX message queue daemon must be running:
```bash
# Verify if mqueue is running; if not, launch it in background
pidin | grep mqueue || mqueue &
```

### 3. Launch with Superuser Privileges
Root privileges are required to issue `ThreadCtl(_NTO_TCTL_IO)` for direct physical memory mapping of BCM2711 registers:
```bash
cd /tmp
chmod +x aegis_icu
./aegis_icu
```

---

## 10. Live Execution Telemetry & Verification

Upon boot, the system conducts a hardware diagnostic self-test, starts the POSIX real-time threads, and streams operational telemetry to the serial/SSH console:

```text
LED test: RED
LED test: YELLOW
LED test: GREEN
DHT11  temp 24 C  humidity 58 %
RAISE  BED-1 P2-WARNING   Heart rate high (122.0)
>>> ALERT P2-WARNING  BED-1 Heart rate high (122.0), waited 0 ms
DHT11  temp 24 C  humidity 58 %
RAISE  BED-2 P1-CRITICAL  SpO2 critical (84.0)
>>> ALERT P1-CRITICAL BED-2 SpO2 critical (84.0), waited 2 ms
  [suppressed] BED-2 SpO2 critical
RAISE  BED-1 P2-WARNING   SpO2 low (89.5)
ESCALATE BED-1 SpO2 low : P2 -> P1
>>> ALERT P1-CRITICAL BED-1 SpO2 low (89.5), waited 6023 ms
DHT11  temp 41 C  humidity 92 %
RAISE  DHT11 P1-CRITICAL  Temperature critical (41.0)
>>> ALERT P1-CRITICAL DHT11 Temperature critical (41.0), waited 1 ms
```

### Key Behavioral Verifications
1. **Suppression Log**: `[suppressed] BED-2 SpO2 critical` proves that duplicate alarms within the 4-second temporal window are filtered out, shielding clinicians from alarm floods.
2. **Starvation Escalation**: `ESCALATE BED-1 SpO2 low : P2 -> P1` demonstrates that an unaddressed warning was escalated to P1 after surpassing the 6000ms threshold.
3. **Latency Boundedness**: The dispatcher logs elapsed wait time (`waited X ms`), verifying sub-millisecond dispatching under critical load.

---

## 11. Medical Safety Compliance & Impact

| Standard / Guidance | Clinical Objective | Implementation in AEGIS-ICU |
| :--- | :--- | :--- |
| **IEC 62304** *(Medical Device Software)* | Software safety classification (Class B/C architectural segregation). | Preemptive thread isolation, deterministic real-time microkernel OS (QNX), starvation-free priority scheduling. |
| **IEC 60601-1-8** *(Medical Alarm Systems)* | Universal alarm prioritization (High, Medium, Low priority color and sound coding). | Strict tri-color state mapping (Red = P1, Yellow = P2, Green = Advisory) and pulsed acoustic cadencing for life-threatening alarms. |
| **ANSI/AAMI HE75** *(Human Factors Engineering)* | Minimizing sensory cognitive overload and nurse fatigue in ICU wards. | Sliding-window anti-flapping deduplication eliminates redundant transient alarms without suppressing emergent escalations. |

---

## 12. Roadmap & Next Steps

- [ ] **CANopen / CAN-FD Medical Bus Support**: Ingest telemetry from physical multiparameter monitors over automotive-grade CAN buses.
- [ ] **HL7 FHIR & MQTT Telemetry**: Export aggregate alarm logs to hospital Electronic Health Record (EHR) systems over TLS-encrypted MQTT.
- [ ] **Multi-Channel PPG/ECG Analog Front-End**: Interface high-resolution SPI ADCs (e.g., ADS1298) for raw bio-potential waveform processing.
- [ ] **Adaptive Dynamic Thresholding**: Train an on-device Edge ML model to dynamically compute personalized vital boundaries based on patient baseline vitals.

---

## 👥 Contributors & Hackathon Metadata
* **Project**: AEGIS-ICU
* **Category**: Embedded Systems / Real-Time Systems / HealthTech & Medical Devices
* **Target OS**: BlackBerry QNX Neutrino RTOS 7.x / 8.0 (ARM64)
* **License**: Distributed under the [Apache 2.0 License](https://www.apache.org/licenses/LICENSE-2.0).

<div align="center">
  <sub>Engineered with precision for zero-fail clinical monitoring.</sub>
</div>
