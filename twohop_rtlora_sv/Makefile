#CROSS_COMPILE = arm-linux-
CC = $(CROSS_COMPILE)gcc
AR = ar
RANLIB = ranlib
RM = rm
MKDIR = mkdir
BACKUP_DIR = Backup
 
ifeq ($(RELEASE), 1)
OBJS_DIR = Release
DBG_FLAGS = -O2 -DNDEBUG
else
OBJS_DIR = Debug
DBG_FLAGS = -g -O0 -DDEBUG
endif
 
DEPEND_FILE = $(OBJS_DIR)/depend_file
 
LIB_NAME = LoRaNetworkServerLibrary
LIB_FULL_NAME = $(OBJS_DIR)/lib$(LIB_NAME).a
 
LIB_SRCS = lora_mac.c application.c aes.c aes_cmac.c crypto.c device_management.c
LIB_SRCS += rtlora_mac.c base64.c parson.c device_mngt.c packet_queue.c schedule_mngt.c trade.c
LIB_SRCS += weather_device.c

LIB_OBJS = $(LIB_SRCS:%.c=$(OBJS_DIR)/%.o)
	
MYSQL_INC = -I/usr/include/mysql
 
LIBS += -l$(LIB_NAME)
LIBS += -lpthread
LIBS += -lm -lmysqlclient 

LIB_DIRS = -L$(OBJS_DIR)

SRCS_DIR = Source
 
DOCUMENT = Document

TARGET_SRCS = lora_network_server.c
TARGET_OBJS = $(TARGET_SRCS:%.c=$(OBJS_DIR)/%.o)
TARGET_NAMES = $(TARGET_SRCS:%.c=$(OBJS_DIR)/%)
 
.SUFFIXES : .c .o
 
all : $(LIB_FULL_NAME) $(TARGET_NAMES)
 
$(LIB_FULL_NAME) : $(LIB_OBJS)
	$(AR) rcv $@ $(LIB_OBJS)
	$(RANLIB) $@
 
$(OBJS_DIR)/%.o : $(SRCS_DIR)/%.c
	@echo "================================================"
	@echo "= Compiling $@ "
	@echo "================================================"
	@`[ -d $(OBJS_DIR) ] || $(MKDIR) $(OBJS_DIR)`
	$(CC) $(CFLAGS) $(DBG_FLAGS) $(MYSQL_INC) -c $< -o $@
 
.SECONDEXPANSION:
$(TARGET_NAMES): $$@.o $(LIB_OBJS)
	$(CC) -o $@ $(TARGET_OBJECTS)$< $(LIB_DIRS) $(LIBS)
 
depend :
	@`[ -d $(OBJS_DIR) ] || $(MKDIR) $(OBJS_DIR)`
	@$(RM) -f $(DEPEND_FILE)
	@for FILE in $(LIB_SRCS:%.c=%) $(TARGET_SRCS:%.c=%); do \
		$(CC) -MM -MT $(OBJS_DIR)/$$FILE.o $(SRCS_DIR)/$$FILE.c >> $(DEPEND_FILE); \
	done

backup :
	@`[ -d $(BACKUP_DIR) ] || $(MKDIR) $(BACKUP_DIR)`
	@echo "Back-up Sorce codes to old directory"
	tar -zcvf lora_networkserver.$(shell date '+%Y-%m-%d_%H%M%S').tgz $(SRCS_DIR) Makefile $(DOCUMENT)
	mv *.tgz $(BACKUP_DIR)
	@echo "Back-up Done"

clean :
	$(RM) -fr $(OBJS_DIR)
 
ifneq ($(MAKECMDGOALS), clean)
ifneq ($(MAKECMDGOALS), depend)
ifneq ($(strip $(LIB_SRCS) $(TARGET_SRCS)),)
-include $(DEPEND_FILE)
endif
endif
endif
