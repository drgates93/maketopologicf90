# maketopologicf90
```
Program: Fortran module dependency analyzer and topologic build order printer.

Usage:
    -d DIRS   Comma-separated list of directories to scan non-recursively.
    -D DIRS   Comma-separated list of directories to scan recursively.
    -m        Print a Makefile dependency list instead of build order.
    -h        Show this help message.

Description:
    Scans .f90 files in given directories to detect Fortran modules and their
    'use' dependencies, then computes a topologic order for building modules.
    Can output either the ordered list of files to build or a Makefile
    dependency list suitable for build systems.

Notes:
    - Repeated use of -d or -D flags is an error.
    - Directories in the flags are comma-separated and will be scanned in the order given
    - If neither -d nor -D is specified, defaults to scanning 'src' non-recursively.

Author:
    Drake Gates
```

# Example makefile to use this progam for all FORTRAN projects
```
# Example makefile to use this progam for all FORTRAN projects
# Compiler and flags
FF = gfortran
FFLAGS = -cpp -fno-align-commons -O3 -ffpe-trap=zero,invalid,underflow,overflow \
         -std=legacy -ffixed-line-length-none -fall-intrinsics \
         -Wno-unused-variable -Wno-unused-function -Wno-conversion -fopenmp
FMOD = -Jmod -Imod
PROGRAM = program

# Directories
LIB     = lib/some_library.a
SRC_DIR = src
OBJ_DIR = obj
MOD_DIR = mod

# Topologically sorted module sources looking recursively through a source directory
TOPOLOGIC_SRC = $(shell maketopologicf90 -D $(SRC_DIR))

# Corresponding object files
OBJECTS = $(foreach src,$(TOPOLOGIC_SRC),$(OBJ_DIR)/$(basename $(notdir $(src))).o)

# Link target
all: $(PROGRAM)

#Build the program
$(PROGRAM): $(OBJECTS)
	$(FF) $(FFLAGS) -o $@ $^ $(LIB)

# Pattern rules
obj/%.o: src/%.f90
	@mkdir -p obj mod
	$(FF) $(FFLAGS) $(FMOD) -c $< -o $@

obj/%.o: src/%.for
	@mkdir -p obj mod
	$(FF) $(FFLAGS) $(FMOD) -c $< -o $@

clean:
	rm -f obj/*.o mod/*.mod $(PROGRAM)

```

# Compiler and flags
```
FF = gfortran
FFLAGS = -cpp -fno-align-commons -O3 -ffpe-trap=zero,invalid,underflow,overflow \
         -std=legacy -ffixed-line-length-none -fall-intrinsics \
         -Wno-unused-variable -Wno-unused-function -Wno-conversion -fopenmp
FMOD = -Jmod -Imod
PROGRAM = some_program
```

# Directories
```
LIB     = lib/some_library.a
SRC_DIR = src
OBJ_DIR = obj
MOD_DIR = mod
```

# Topologically sorted module sources looking recursively through a source directory
```
TOPOLOGIC_SRC = $(shell maketopologicf90 -D $(SRC_DIR))
```

# Corresponding object files
```
OBJECTS = $(foreach src,$(TOPOLOGIC_SRC),$(OBJ_DIR)/$(basename $(notdir $(src))).o)
```

# Link target
```
all: $(PROGRAM)

#Build the program
$(PROGRAM): $(OBJECTS)
	$(FF) $(FFLAGS) -o $@ $^ $(LIB)
```
# Pattern rules
```
obj/%.o: src/%.f90
	@mkdir -p obj mod
	$(FF) $(FFLAGS) $(FMOD) -c $< -o $@

obj/%.o: src/%.for
	@mkdir -p obj mod
	$(FF) $(FFLAGS) $(FMOD) -c $< -o $@
```

# Clean up
```
clean:
	rm -f obj/*.o mod/*.mod $(PROGRAM)
```
