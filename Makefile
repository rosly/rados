include target.mk
include $(PORT)/target.mk

SOURCES = \
	os_sched.c \
	os_sem.c \
	os_mtx.c \
	os_timer.c \
	$(PORTSOURCES)

INCLUDES = . $(PORT)
BUILDDIR = build

vpath %.c . $(PORT)
vpath %.o $(BUILDDIR)
vpath %.elf $(BUILDDIR)
DEPEND = $(addprefix $(BUILDDIR)/, $(SOURCES:.c=.d))
OBJECTS = $(addprefix $(BUILDDIR)/, $(SOURCES:.c=.o))
LISTINGS = $(addprefix $(BUILDDIR)/, $(SOURCES:.c=.lst))
TARGET = $(BUILDDIR)/libkernel.a

all: $(TARGET) size
lst: $(LISTINGS)

$(TARGET): $(OBJECTS)
	@echo -e "[AR]\t$@"
	@$(RM) $@
	@$(AR) -cq $@ $^

$(BUILDDIR)/%.o: %.c
	@echo -e "[CC]\t$<"
	@$(CC) -c $(CFLAGS) -o $@ $(addprefix -I, $(INCLUDES)) $<

$(BUILDDIR)/%.lst: %.o
	@echo -e "[LST]\t$<"
	@$(OBJDUMP) -dStw $< > $@

size: $(TARGET)
	@echo -e "[SIZE]\t$^"
	@$(SIZE) -t $^

# include the dependencies unless we're going to clean, then forget about them.
ifneq ($(MAKECMDGOALS), clean)
-include $(DEPEND)
endif
# dependencies file
# includes also considered, since some of these are our own
# (otherwise use -MM instead of -M)
$(BUILDDIR)/%.d: %.c
	@echo -e "[DEP]\t$<"
	@$(CC) -M ${CFLAGS} $(addprefix -I, $(INCLUDES)) $< >$@

.PHONY: clean test lst size

clean:
	@$(RM) $(TARGET); echo -e "[RM]\t$(TARGETS)"
	@$(RM) $(OBJECTS); echo -e "[RM]\t$(OBJECTS)"
	@$(RM) $(DEPEND); echo -e "[RM]\t$(DEPEND)"
	@$(RM) $(LISTINGS); echo -e "[RM]\t$(LISTINGS)"
	@$(MAKE) --no-print-directory -C test clean

test: $(TARGET)
	@$(MAKE) --no-print-directory -C test
