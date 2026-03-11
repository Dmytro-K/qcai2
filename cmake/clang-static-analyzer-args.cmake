# Extra scan-build arguments are configured here so checker selection and analyzer
# tuning can be adjusted without editing the main CMakeLists.txt.
set(QCAI2_CLANG_STATIC_ANALYZER_SCAN_BUILD_ARGS
    --status-bugs
    -enable-checker
    alpha.core.CastSize
    -enable-checker
    alpha.security.ArrayBoundV2
    -enable-checker
    alpha.unix.Stream
    -analyzer-config
    max-nodes=75000

    # Example checker selection:
    # -enable-checker
    # alpha.unix.Stream
    #
    # Example analyzer config:
    # -analyzer-config
    # max-nodes=75000
)
