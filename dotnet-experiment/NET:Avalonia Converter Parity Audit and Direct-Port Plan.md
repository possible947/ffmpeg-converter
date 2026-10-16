.NET/Avalonia Converter Parity Audit and Direct-Port Plan

Context

The project contains:

1. The native C converter implementation.
2. The .NET/C#/Avalonia converter implementation.
3. The legacy Windows Pascal implementation.

The native C implementation is the authoritative reference.

The .NET implementation is intended to replace the Windows Pascal version.
Its current state is behind the native C implementation:

- HQ integration exists;
- current Mux Policy is not fully ported;
- Add Track and mux behavior predate the completed Phase 3.2 work;
- Copy is exposed too broadly;
- recent MKV, MOV, M4V, preservation, validation and UI fixes may be missing.

The synchronization method is direct behavioral porting.

This task is an audit and planning pass only.

Do not implement the port during this task.

======================================================================
1. Authoritative implementation
======================================================================

The native C implementation is the only source of truth.

Required direction:

    current native C behavior
        →
    direct behavioral port
        →
    .NET/Avalonia parity

Do not redesign existing behavior for C# or Avalonia.

Do not preserve older .NET behavior when it differs from the current C
implementation.

Do not use the Pascal implementation as a behavioral reference.

Do not attempt three-way parity between C, .NET and Pascal.

======================================================================
2. Strict prohibition on native C changes
======================================================================

The native C implementation is read-only.

Do not modify:

- C sources or headers;
- Objective-C sources;
- CMake files;
- native tests;
- presets or registries used by the C version;
- native resources;
- native documentation;
- native build logic.

Do not add helper APIs to the C implementation merely to simplify the .NET
port.

If a possible C defect is found:

1. record it separately;
2. provide exact evidence;
3. do not fix it;
4. continue using current C behavior as the parity reference unless its
   intended behavior cannot be determined.

Verify before and after the audit that no native C file changed.

======================================================================
3. No implementation during this pass
======================================================================

This task must stop after comparison and planning.

Do not:

- port production code;
- introduce .NET Mux Policy classes;
- change Avalonia controls;
- modify validation;
- modify execution routing;
- change Add Track;
- change M4V behavior;
- change HQ integration;
- upgrade packages;
- create functional commits.

Reading, building, testing and collecting temporary evidence are allowed.

======================================================================
4. Repository-state audit
======================================================================

Before analysis:

1. Record repository paths.
2. Record branches and HEAD commits for C and .NET.
3. Run git status for both.
4. Record pre-existing modifications.
5. Do not reset, stash or overwrite user changes.
6. Determine whether implementations are stored:
   - in one repository;
   - in separate directories;
   - in separate repositories;
   - through submodules;
   - through shared resources.
7. Identify files shared by both implementations.

Create a protected-file inventory for the native C implementation.

======================================================================
5. Reference architecture boundaries
======================================================================

The current native application contains two different execution models.

Mux/HQ:

    Build Profile
        →
    Mux Policy
        →
    Add Track / HQ handoff
        →
    MKV / MOV / M4V

One-pass conversion:

    presets and selection data
        +
    runtime auto-detection
        +
    ffmpeg_command_builder
        →
    one FFmpeg execution

Do not apply Mux Policy indiscriminately to:

- Software;
- VideoToolbox;
- VAAPI;
- NVENC;
- QSV;
- AMF;
- Vulkan;
- other one-pass backends.

======================================================================
6. Audio-mode boundary
======================================================================

The reference common Audio Core modes are:

    PCM
    AAC
    AAC+AC3

Copy is not a common Audio Core mode.

Copy belongs exclusively to Mux/HQ and is available only where Mux Policy
explicitly permits it.

Expected boundary:

    one-pass modes:
        PCM
        AAC
        AAC+AC3

    Mux/HQ:
        choices and defaults from Mux Policy
        including Copy only where permitted

Compare how the .NET implementation defines, exposes, validates and executes
audio modes.

Determine whether .NET:

- exposes Copy globally;
- validates Copy before execution;
- passes Copy into a one-pass command builder;
- silently normalizes Copy;
- allows FFmpeg to reject it later;
- contains an older defensive override.

======================================================================
7. M4V workflow distinction
======================================================================

Direct M4V and Mux M4V are separate workflows.

Direct M4V:

    dedicated workflow
        →
    settings dialog
        →
    explicit language field
        →
    direct M4V creation

Mux M4V:

    Mux Policy
        →
    Add Track
        →
    autonomous M4V finalization

Do not classify differences in language-tag selection between these workflows
as parity defects.

The language field in the direct M4V dialog is intentional behavior.

The .NET port must reproduce each current C workflow independently.

Do not force Direct M4V and Mux M4V to have identical UX.

======================================================================
8. Functional architecture mapping
======================================================================

Map the native C implementation by functional area:

- selection catalog;
- presets and registry;
- audio availability;
- Audio Orchestrator;
- ffmpeg command construction;
- one-pass Converter workflow;
- Copy mode;
- Mux Policy;
- Add Track;
- MKV;
- MOV;
- M4V;
- direct M4V workflow;
- stream preservation;
- replacement-stream isolation;
- HQ adapter and handoff;
- CLI validation;
- GTK exposure;
- native macOS exposure;
- runtime capability discovery;
- queue behavior;
- error propagation;
- output naming and verification.

Map corresponding .NET/Avalonia areas:

- models;
- services;
- view models;
- views;
- preset loaders;
- selection providers;
- command builders;
- process runners;
- media-probe services;
- validation services;
- HQ services;
- mux services;
- runtime discovery;
- tests.

For each functional block record:

    C source and functions
        →
    .NET counterpart
        →
    parity status

Use:

    MATCH
    PARTIAL
    MISSING
    DIFFERENT
    LEGACY
    UNKNOWN
    NOT APPLICABLE

======================================================================
9. Mux Policy comparison
======================================================================

Document the native C Mux Policy contract:

- operational modes;
- containers;
- extensions;
- audio allowlists;
- defaults;
- Copy scope;
- first-track versus all-track behavior;
- replacement codec rules;
- HQ rules;
- subtitle compatibility;
- chapter handling;
- metadata handling;
- attachment handling;
- language and dispositions;
- validation errors.

For each rule identify:

    C rule
    C source/function
    .NET equivalent
    .NET source/member
    parity status
    required porting unit

Determine whether .NET currently uses:

- centralized policy;
- scattered conditions;
- UI-only filtering;
- preset fields;
- execution-time choices;
- no equivalent rule.

======================================================================
10. Add Track comparison
======================================================================

Reference workflow:

    validate replacement
        →
    probe replacement video codec
        →
    resolve Mux Policy
        →
    prepare safe MKV intermediate
        →
    verify preparation success
        →
    combine replacement video with source streams
        →
    finalize MKV / MOV / M4V
        →
    validate output

Compare .NET behavior for:

- replacement input validation;
- video-only replacement contribution;
- safe intermediate;
- failure propagation;
- source versus replacement ownership;
- final-output verification;
- temporary-file cleanup;
- cancellation;
- progress;
- errors.

Check whether .NET:

- prepares source directly in the final MOV or M4V container;
- continues after source-preparation failure;
- accepts non-video replacement input;
- imports replacement auxiliary streams;
- lacks output verification.

======================================================================
11. Container behavior comparison
======================================================================

MKV reference:

- replacement contributes video only;
- Copy preserves all source audio;
- PCM uses the first source audio track;
- AAC uses the first source audio track;
- AAC+AC3 derives both outputs from the first source audio track;
- source subtitles, chapters, metadata and attachments are preserved;
- replacement auxiliary streams are excluded;
- incompatible subtitles receive container-aware handling.

MOV reference:

- uses the shared safe-intermediate contract;
- final video comes from replacement;
- audio follows MOV Policy;
- HQ ProRes resolves to MOV + PCM;
- replaced source video does not need to be compatible with final MOV;
- preparation failure stops post-mux;
- supported source streams and metadata follow current Policy.

M4V reference:

- explicit AAC;
- explicit AAC+AC3;
- default AAC+AC3;
- PCM and Copy rejected;
- H.264 replacement;
- ProRes source allowed when replaced by compatible video;
- safe intermediate;
- chapter and subtitle handling;
- practical metadata handling;
- safe paths with spaces, plus signs, parentheses and non-ASCII characters.

Compare every item with .NET.

======================================================================
12. HQ integration comparison
======================================================================

Reference flow:

    HQ job
        →
    generated video-track path
        →
    shared Add Track
        →
    shared Mux Policy
        →
    MKV or MOV

Compare:

- x264;
- x265;
- ProRes;
- applicable SVT modes;
- final-container choice;
- audio availability;
- ProRes PCM-only rule;
- preset preservation;
- Add Track refresh;
- invalid replacement gating;
- failure propagation;
- absence of a separate HQ-specific mux path.

======================================================================
13. Stream-preservation comparison
======================================================================

Compare:

- subtitles;
- subtitle/container compatibility;
- chapters;
- global metadata;
- stream metadata;
- language;
- default disposition;
- forced disposition;
- attachments;
- source/replacement ownership.

For every category record the C action:

    preserve
    copy
    convert
    drop with warning
    reject

Pay particular attention to:

- mov_text to MKV;
- source subtitles in MOV;
- M4V subtitles;
- source attachments in MKV;
- replacement attachments;
- replacement chapters and tags;
- process-argument quoting.

Do not classify the direct-M4V language dialog as a defect.

======================================================================
14. UI and programmatic validation
======================================================================

Compare .NET CLI, services and Avalonia UI with native C behavior.

Check:

- encoder availability;
- audio choices;
- defaults;
- Add Track requirement;
- replacement validation;
- Convert-button state;
- preservation of selected video preset;
- timing of validation;
- whether invalid requests launch FFmpeg.

Required parity:

- UI exposes valid choices;
- programmatic requests are independently validated;
- invalid requests fail before external execution;
- valid selections survive unrelated UI refresh;
- Copy is visible only in Mux/HQ contexts allowed by Policy.

======================================================================
15. Runtime discovery
======================================================================

Compare discovery of:

- FFmpeg;
- ffprobe;
- mkvmerge;
- MP4Box;
- HQ runtime;
- platform encoders;
- bundled and external tools;
- runtime capabilities.

Record precedence and fallbacks.

Identify assumptions inherited from the Windows Pascal implementation.

Do not modify discovery behavior during the audit.

======================================================================
16. Queue and error behavior
======================================================================

Compare:

- valid input;
- corrupt input;
- another valid input;
- cancellation;
- single-source Add Track failure;
- partial-output cleanup;
- normal queue continuation;
- result reporting;
- GUI status.

Reference distinction:

    normal conversion queue
        may continue per file

    single-source Add Track preparation
        must succeed before post-mux

======================================================================
17. Build and test inventory
======================================================================

Where the current environment permits:

Native C:

- build from a clean directory;
- run available native tests;
- record results;
- do not change native files.

.NET:

- restore using declared repository configuration;
- build relevant projects;
- run all test projects;
- record warnings and failures;
- run Avalonia where supported.

Do not upgrade packages.

Do not fix failures during this pass.

======================================================================
18. Representative black-box comparison
======================================================================

Where both implementations can run, use identical fixtures.

Minimum matrix:

- one-pass PCM;
- one-pass AAC;
- one-pass AAC+AC3;
- invalid one-pass Copy;
- Mux MKV Copy;
- Mux MKV PCM;
- Mux MOV PCM;
- Mux M4V AAC;
- Mux M4V AAC+AC3;
- Mux M4V default;
- HQ x264;
- HQ ProRes;
- direct M4V where available.

Compare:

- acceptance or rejection;
- validation error;
- external commands;
- container;
- video codec;
- audio tracks;
- chapters;
- subtitles;
- metadata;
- dispositions;
- attachments.

Do not compare files byte-for-byte.

======================================================================
19. Development and validation strategy
======================================================================

Windows is not the primary .NET/Avalonia development or debugging environment.

The .NET implementation must be completed and tested as fully as possible
before it is transferred to Windows.

Required sequence:

    reference C implementation
        →
    direct .NET/Avalonia port
        →
    parity testing outside native Windows
        →
    Linux / WSL hardware validation
        →
    functionally complete .NET solution
        →
    native Windows release validation

Do not use this development model:

    partial port
        →
    debug on Windows
        →
    return for more implementation
        →
    repeat

Known functional gaps must not be intentionally deferred to Windows.

======================================================================
20. State required before Windows
======================================================================

Before native Windows validation, complete:

- current C behavior transfer;
- Mux Policy parity;
- Add Track parity;
- MKV, MOV and M4V parity;
- HQ handoff parity;
- Copy isolation;
- Audio Core parity;
- stream-preservation parity;
- early validation;
- CLI and Avalonia consistency;
- applicable C test transfer;
- black-box comparison using identical fixtures.

Windows must not be used to design missing general business logic.

======================================================================
21. Role of Windows
======================================================================

Windows is the final integration and release-validation environment for the
completed .NET implementation.

Native Windows validation covers:

- .NET/Avalonia application execution;
- packaging;
- runtime delivery;
- native process behavior;
- path and filesystem behavior;
- platform tool discovery;
- hardware encoders;
- final media workflows;
- readiness to replace the Pascal application.

A general logic defect found on Windows must be compared with native C
behavior. Do not invent separate Windows behavior to hide parity defects.

======================================================================
22. Windows-specific hardware scope
======================================================================

The primary hardware capabilities unique to the final Windows validation are:

    CUDA
    Intel Media

These are platform-specific extensions of the already verified common
Converter behavior.

They are not a separate Converter architecture.

All non-unique functionality must be completed before the native Windows
release run.

======================================================================
23. Preliminary validation through WSL
======================================================================

The target Windows hardware is also used through Linux/WSL for preliminary
hardware verification.

Where possible, validate before native Windows release testing:

- NVIDIA/CUDA discovery;
- CUDA compatibility with FFmpeg and toolchain;
- Intel Media availability;
- hardware capability probes;
- hardware media scenarios;
- behavior on the same physical GPUs in Linux/WSL.

Required sequence:

    Linux / WSL on target Windows host
        →
    CUDA and Intel Media validation
        →
    hardware configuration confirmed
        →
    native Windows release build
        →
    final integration validation

WSL verification separates hardware and FFmpeg/toolchain issues from
Windows application integration.

======================================================================
24. Windows-entry criteria
======================================================================

Native Windows validation may begin only when:

1. .NET behavior is synchronized with current native C behavior.
2. All applicable parity tests pass.
3. Mux/HQ and one-pass modes have expected behavior.
4. No known general functional gaps are intentionally deferred to Windows.
5. Hardware on the target Windows machine has been checked through Linux/WSL
   where possible.
6. The .NET/Avalonia application is considered functionally complete.
7. Remaining unknowns are inherently Windows-specific.

The Windows run is release acceptance, not continuation of implementation.

======================================================================
25. Direct-port units
======================================================================

Break missing parity into dependency-correct units:

1. Shared audio-mode availability and Copy isolation.
2. Mux Policy model and pure resolver.
3. Replacement codec model and probing.
4. Add Track validation.
5. Safe source intermediate.
6. MKV post-mux.
7. MOV finalization.
8. M4V finalization.
9. Direct M4V workflow.
10. Stream preservation.
11. HQ handoff.
12. Avalonia availability and control state.
13. Programmatic and CLI validation.
14. Output verification.
15. Production-path parity tests.
16. WSL hardware validation preparation.
17. Windows release-validation preparation.

For each unit define:

- C references;
- .NET targets;
- prerequisites;
- behavior to port;
- behavior not to port;
- tests;
- acceptance criteria;
- risks;
- adapt or replace decision.

Do not provide implementation code during the audit.

======================================================================
26. Proposed dependency order
======================================================================

Phase A: Pure policy parity

- audio availability;
- Copy isolation;
- Mux modes and defaults;
- validation;
- replacement codec rules;
- pure tests.

Phase B: Shared Add Track core

- replacement probing;
- source/replacement ownership;
- safe intermediate;
- failure propagation.

Phase C: Container execution

- MKV;
- MOV;
- M4V;
- output validation.

Phase D: Preservation

- subtitles;
- chapters;
- metadata;
- language;
- dispositions;
- attachments.

Phase E: HQ integration

- shared handoff;
- x264/x265/ProRes;
- applicable SVT;
- no HQ-specific mux path.

Phase F: Avalonia and programmatic exposure

- valid choices;
- defaults;
- Add Track state;
- preset preservation;
- early validation.

Phase G: End-to-end parity outside Windows

- identical fixtures;
- C/.NET comparison;
- full available test suite.

Phase H: Linux/WSL hardware preparation

- CUDA;
- Intel Media;
- runtime probes;
- FFmpeg/toolchain compatibility.

Phase I: Windows release validation

- packaging;
- process integration;
- hardware backends;
- final workflows;
- Pascal replacement readiness.

======================================================================
27. Required report
======================================================================

Create one Markdown report:

    dotnet-c-parity-audit-and-port-plan.md

Required sections:

A. Executive summary  
B. Repository state  
C. Architecture map  
D. Behavioral parity matrix  
E. Audio availability and Copy scope  
F. Mux Policy comparison  
G. Add Track comparison  
H. MKV comparison  
I. MOV comparison  
J. M4V comparison  
K. Direct M4V comparison  
L. HQ comparison  
M. Stream-preservation comparison  
N. CLI/Avalonia comparison  
O. Runtime-discovery comparison  
P. Queue/error comparison  
Q. Existing test comparison  
R. Direct-porting plan  
S. Development and platform-validation strategy  
T. WSL hardware-validation plan  
U. Native Windows release-validation plan  
V. Proposed commit sequence  
W. Explicit non-goals  
X. Open questions

Behavioral matrix columns:

    functional area
    C behavior
    C source
    .NET behavior
    .NET source
    parity status
    evidence
    proposed port unit

======================================================================
28. Non-goals
======================================================================

Do not:

- change native C;
- change Pascal;
- remove Pascal during this audit;
- upgrade packages;
- redesign unrelated Avalonia architecture;
- redesign one-pass backends;
- add features;
- unify Direct M4V and Mux M4V UX;
- defer known general functional gaps to Windows;
- use native Windows as the primary development loop.

======================================================================
29. Commit policy
======================================================================

Do not create implementation commits.

By default:

- leave the report uncommitted;
- keep evidence under ignored build locations;
- stage nothing;
- preserve user changes;
- modify no C files.

At completion show:

    git status for C
    git status for .NET
    files created by audit
    confirmation native C is unchanged
    confirmation Pascal is unchanged

======================================================================
30. Completion criteria
======================================================================

The audit is complete when:

✓ native C is protected as read-only;

✓ .NET boundaries are mapped;

✓ audio availability and Copy scope are compared;

✓ Direct M4V and Mux M4V are treated separately;

✓ Mux Policy is compared;

✓ Add Track is compared;

✓ MKV, MOV and M4V are compared;

✓ HQ is compared;

✓ preservation is compared;

✓ CLI/Avalonia validation is compared;

✓ runtime discovery is compared;

✓ queue/error behavior is compared;

✓ tests are inventoried;

✓ representative black-box comparison is performed where possible;

✓ every gap maps to a dependency-correct port unit;

✓ Linux/WSL hardware validation is planned;

✓ native Windows release validation is planned;

✓ no implementation is changed;

✓ no native C file is changed;

✓ Pascal remains unchanged;

✓ no functional commit is created.

Final instruction

This is an analysis and planning task only.

Stop after producing the comparison report and direct-port plan.

Wait for explicit plan approval before changing the .NET/Avalonia
implementation.

The native C implementation remains immutable and authoritative.

Windows is the final release-validation environment, not the primary
development environment.
