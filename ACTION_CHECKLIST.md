# Action Checklist for central_app Branch Improvements

This checklist provides actionable tasks based on the code review. Copy this to your issue tracker or use it to track progress.

## 🔴 Critical Priority (Security & Stability)

### Security Configuration
- [ ] Review security requirements for the battery service application
- [ ] Enable appropriate pairing mode in `gapm_cfg` (GAPM_PAIRING_LEGACY or GAPM_PAIRING_SEC_CON)
- [ ] Configure privacy settings if required
- [ ] Implement security callbacks properly
- [ ] Document security design decisions in README
- [ ] Test pairing and encryption functionality

**File**: `samples/bluetooth/common/central_itf.c`
**Lines**: ~68-82 (gapm_cfg structure)

### Test Infrastructure
- [ ] Create test directory structure (`tests/bluetooth/le_central_batt/`)
- [ ] Add testcase.yaml configuration
- [ ] Create test CMakeLists.txt
- [ ] Implement mock interfaces for GAP/GATT APIs
- [ ] Write unit tests for state machine logic
- [ ] Write unit tests for battery client callbacks
- [ ] Write integration test for full connection flow
- [ ] Add test documentation to README

### Error Recovery
- [ ] Add retry logic for service discovery failures
- [ ] Implement connection retry with exponential backoff
- [ ] Add error callback mechanism for upper layers
- [ ] Handle all error codes from BLE API calls
- [ ] Document error handling strategy
- [ ] Test all error paths

**Files**: 
- `samples/bluetooth/common/batt_cli.c` (service_discovery function)
- `samples/bluetooth/common/central_itf.c` (connection management)

---

## ⚠️ High Priority (Robustness & Maintainability)

### Function Documentation
- [ ] Add Doxygen-style comments to all public functions in `central_itf.h`
- [ ] Add Doxygen-style comments to all public functions in `batt_cli.h`
- [ ] Add Doxygen-style comments to `client_profile_manager.h`
- [ ] Document internal functions in implementation files
- [ ] Add module-level documentation blocks
- [ ] Document callback contracts (when they're called, what they should do)
- [ ] Generate Doxygen documentation and review

**Files**: All header and source files

### Resource Management
- [ ] Implement `central_itf_deinit()` function
- [ ] Add cleanup logic for scan activity
- [ ] Add cleanup logic for init activity
- [ ] Add proper disconnect handling
- [ ] Clear semaphores on cleanup
- [ ] Add resource leak testing
- [ ] Document resource lifecycle

**File**: `samples/bluetooth/common/central_itf.c`

**Suggested function signature**:
```c
void central_itf_deinit(void);
```

### Synchronization
- [ ] Identify all shared data structures
- [ ] Add mutex for `central_env` structure access
- [ ] Create helper functions for thread-safe access
- [ ] Document threading model in module header
- [ ] Add atomic operations for simple flags
- [ ] Review all callbacks for race conditions
- [ ] Test with thread sanitizer if available

**File**: `samples/bluetooth/common/central_itf.c`

### Complete Error Handling
- [ ] Review all API calls for unchecked return values
- [ ] Add error logging for all failure paths
- [ ] Ensure all callbacks handle error status codes
- [ ] Add parameter validation in all callbacks
- [ ] Return appropriate error codes from all functions
- [ ] Document expected error behavior

**Files**: All source files

---

## 💡 Medium Priority (Configuration & Usability)

### Kconfig Integration
- [ ] Create Kconfig file for le_central_batt sample
- [ ] Add scan interval configuration option
- [ ] Add scan window configuration option
- [ ] Add connection interval configuration options
- [ ] Add supervision timeout configuration
- [ ] Add device name configuration
- [ ] Update CMakeLists.txt to use Kconfig values
- [ ] Document Kconfig options

**New file**: `samples/bluetooth/le_central_batt/Kconfig`

**Example**:
```kconfig
config BLE_CENTRAL_SCAN_INTERVAL_MS
    int "BLE scan interval in milliseconds"
    default 37
    range 3 10240

config BLE_CENTRAL_SCAN_WINDOW_MS
    int "BLE scan window in milliseconds"  
    default 30
    range 3 10240
```

### Replace Magic Numbers
- [ ] Define constants for all connection parameters
- [ ] Define constants for timeouts
- [ ] Define constants for scan parameters (already done, verify completeness)
- [ ] Add helper macros for unit conversions (MS_TO_CONN_INTERVAL, etc.)
- [ ] Update all hardcoded values to use named constants
- [ ] Document units in constant names or comments

**Files**: 
- `samples/bluetooth/common/central_itf.c`

### Input Validation
- [ ] Add NULL pointer checks in all callbacks
- [ ] Validate connection index range
- [ ] Validate activity indices
- [ ] Validate battery instance numbers
- [ ] Add parameter assertions for debug builds
- [ ] Document parameter constraints

**Files**: 
- `samples/bluetooth/common/batt_cli.c`
- `samples/bluetooth/common/central_itf.c`

### Enhanced README
- [ ] Add prerequisites section
- [ ] Add building instructions with different configurations
- [ ] Add troubleshooting section
- [ ] Document expected output/behavior
- [ ] Add sequence diagram for BLE operations
- [ ] Document configuration options
- [ ] Add known limitations section
- [ ] Add FAQ section

**File**: `samples/bluetooth/le_central_batt/README.rst`

---

## 📝 Low Priority (Polish & Future Enhancements)

### Header File Improvements
- [ ] Add include guards verification
- [ ] Add comprehensive file header documentation
- [ ] Add version information
- [ ] Add deinit function declarations
- [ ] Document all constants
- [ ] Add example usage in comments
- [ ] Verify all headers are self-contained

**Files**: 
- `samples/bluetooth/common/central_itf.h`
- `samples/bluetooth/common/batt_cli.h`
- `samples/bluetooth/common/client_profile_manager.h`

### Connection Parameter Optimization
- [ ] Research optimal parameters for battery applications
- [ ] Consider adding slave latency for power savings
- [ ] Benchmark different connection interval values
- [ ] Document parameter tradeoffs
- [ ] Add power consumption measurements
- [ ] Consider adaptive parameter adjustment

**File**: `samples/bluetooth/common/central_itf.c`

### Logging Refinement
- [ ] Replace remaining printk() calls with LOG_DBG()
- [ ] Review and adjust logging levels
- [ ] Add compile-time log level configuration
- [ ] Ensure sensitive data not logged
- [ ] Add log filtering capabilities
- [ ] Document logging strategy

**Files**: All source files

### Code Style Consistency
- [ ] Run clang-format on all files
- [ ] Verify indentation consistency
- [ ] Check naming conventions
- [ ] Verify line length limits
- [ ] Review and apply project coding standards
- [ ] Set up automated style checking

**Files**: All source files

---

## 🧪 Validation Tasks

### Testing Checklist
- [ ] Test successful connection and battery reading
- [ ] Test connection failure scenarios
- [ ] Test service discovery failures
- [ ] Test disconnection and reconnection
- [ ] Test with device not advertising
- [ ] Test with wrong device name
- [ ] Test with no battery service
- [ ] Test notification functionality
- [ ] Test multiple connect/disconnect cycles
- [ ] Test with encryption enabled
- [ ] Perform stress testing
- [ ] Test on target hardware

### Code Quality Checks
- [ ] Run static analysis (if available)
- [ ] Check for memory leaks with valgrind or similar
- [ ] Run clang-tidy or similar linter
- [ ] Check test coverage percentage
- [ ] Review all compiler warnings
- [ ] Check for thread safety issues
- [ ] Profile performance if needed

### Documentation Review
- [ ] Verify all public APIs documented
- [ ] Check documentation builds without errors
- [ ] Verify code examples in docs are correct
- [ ] Get peer review on documentation
- [ ] Check spelling and grammar
- [ ] Verify diagrams are accurate and up to date

---

## 📊 Progress Tracking

### Overall Status

```
Critical:  [ ] 0/3 complete (0%)
High:      [ ] 0/4 complete (0%)
Medium:    [ ] 0/4 complete (0%)
Low:       [ ] 0/4 complete (0%)
```

### Estimated Effort

| Priority | Tasks | Est. Hours | Status |
|----------|-------|------------|--------|
| Critical | 3 | 16-24 | Not Started |
| High | 4 | 12-16 | Not Started |
| Medium | 4 | 8-12 | Not Started |
| Low | 4 | 4-8 | Not Started |
| **Total** | **15** | **40-60** | **Not Started** |

---

## 🎯 Suggested Implementation Order

### Week 1: Critical Items
1. **Day 1-2**: Security configuration and testing
2. **Day 3-4**: Basic test infrastructure
3. **Day 4-5**: Error recovery mechanisms

### Week 2: High Priority Items  
1. **Day 1-2**: Function documentation
2. **Day 3**: Resource management
3. **Day 4**: Synchronization
4. **Day 5**: Complete error handling

### Week 3: Medium Priority Items
1. **Day 1-2**: Kconfig integration
2. **Day 2-3**: Replace magic numbers  
3. **Day 3-4**: Input validation
4. **Day 4-5**: Enhanced README

### Week 4: Low Priority & Polish
1. **Day 1**: Header file improvements
2. **Day 2**: Connection parameter optimization
3. **Day 3**: Logging refinement
4. **Day 4-5**: Code style and final validation

---

## 📋 Quick Win Tasks (Can be done in parallel)

These tasks are independent and can be tackled by different team members:

- [ ] Replace `printk()` with `LOG_DBG()` (15 min)
- [ ] Run clang-format (5 min)
- [ ] Add include guards verification (10 min)
- [ ] Add NULL pointer checks in callbacks (30 min)
- [ ] Document scan parameter constants (15 min)
- [ ] Add troubleshooting section to README (30 min)
- [ ] Create Kconfig file skeleton (20 min)
- [ ] Add function header comments (2 hours)

---

## 🔗 Related Documents

- Detailed review: `COMMIT_REVIEW_SUGGESTIONS.md`
- Executive summary: `REVIEW_SUMMARY.md`
- Code evolution: `CODE_EVOLUTION_ANALYSIS.md`

---

## ✅ Definition of Done

A task is complete when:
- [ ] Code changes implemented
- [ ] Unit tests added/updated (if applicable)
- [ ] Documentation updated
- [ ] Code review completed
- [ ] CI/CD pipeline passes
- [ ] Tested on target hardware
- [ ] Committed with descriptive message

---

**Created**: October 20, 2025  
**Based on**: Code review of central_app branch commits 7299f82, d5ca0a4, 153cdce  
**Priority**: Address Critical items before production deployment
