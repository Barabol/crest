COMPILER = g++
DEBUGGER = gdb

OUTPUT_FILE = crest.o
RUN_ARGS = 
COMPILATION_DIR = ./

FILES :=  
DEPENDENCIES := ./*.h ./*.c 

MEMTESTER := valgrind
MEMTEST_OPT := --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=memtest.log

build:
	$(COMPILER) $(FILES) $(DEPENDENCIES) -o $(OUTPUT_FILE)

run:
	$(COMPILATION_DIR)$(OUTPUT_FILE) $(RUN_ARGS)

debug:
	$(COMPILER) $(FILES) $(DEPENDENCIES) -o $(OUTPUT_FILE) -p 
	$(DEBUGGER) --args $(COMPILATION_DIR)$(OUTPUT_FILE) $(RUN_ARGS)

memtest: build
	$(MEMTESTER) $(MEMTEST_OPT) $(COMPILATION_DIR)$(OUTPUT_FILE) $(RUN_ARGS) 
	cat memtest.log

procinfo:
	/usr/bin/time -v $(COMPILATION_DIR)$(OUTPUT_FILE) $(RUN_ARGS) 

lines:
	echo "Header files lines: ";\
	find . -name '*.h' | sed 's/.*/"&"/' | xargs wc -l;\
	echo "C files lines: ";\
	find . -name '*.c' | sed 's/.*/"&"/' | xargs wc -l
