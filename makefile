CC		= gcc
GREEN		= \033[0;32m
NC		= \033[0m
LOG_PREFIX	= $(GREEN)💬$(NC)
SOURCE		= main.c semaphore_utils.c
OUTFILE		= mycopy
TESTFILE	= ./testinput
TIME_FORMAT	= Total time elapsed: %e sec, CPU percentage: %P, Context switch: %w time(s)
RUN_COMMAND	= ./$(OUTFILE) $(TESTFILE)

main: $(OUTFILE)
	@echo "$(LOG_PREFIX) Start running: $(RUN_COMMAND)"
	@time -f "   $(TIME_FORMAT)" $(RUN_COMMAND)
	@echo "$(LOG_PREFIX) Finished running!"
	@echo "$(LOG_PREFIX) Showing diff... (no output if program runs as expected)"
	@diff $(TESTFILE) out

build_only: $(OUTFILE)

$(OUTFILE): $(SOURCE)
	@echo "$(LOG_PREFIX) Source outdated, re-building..."
	@$(CC) $(SOURCE) -o $(OUTFILE)
	@echo "$(LOG_PREFIX) Re-build complete without error!"
