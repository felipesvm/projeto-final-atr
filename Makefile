# Sistema de logística colaborativa com AMRs — Etapa 1
#   make          compila tudo em bin/
#   make testes   compila e executa os testes de domínio (T-01)
#   make tsan     compila com ThreadSanitizer em bin-tsan/
#   make clean    remove os artefatos

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g -Wall -Wextra -Wpedantic -Wshadow
CPPFLAGS += -MMD -MP
LDLIBS   += -pthread -lrt

BUILD ?= build
BIN   ?= bin

COMUM_SRC   := $(wildcard src/comum/*.cpp)
COMUM_OBJ   := $(COMUM_SRC:%.cpp=$(BUILD)/%.o)
LIB_COMUM   := $(BUILD)/libamr_comum.a

ROBO_OBJ    := $(patsubst %.cpp,$(BUILD)/%.o,$(wildcard src/robo/*.cpp))
ORG_OBJ     := $(patsubst %.cpp,$(BUILD)/%.o,$(wildcard src/organizador/*.cpp))
GESTOR_OBJ  := $(patsubst %.cpp,$(BUILD)/%.o,$(wildcard src/gestor/*.cpp))
COLETOR_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(wildcard src/coletor/*.cpp))
FERRAMENTAS := anunciar_robo injetar_evento amr_monitor limpar_ipc rajada_comandos gerar_mapa

PROGRAMAS := $(BIN)/amr_organizador $(BIN)/amr_gestor $(BIN)/amr_robo $(BIN)/amr_coletor \
             $(addprefix $(BIN)/,$(FERRAMENTAS))

.PHONY: all testes tsan clean

all: $(PROGRAMAS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(LIB_COMUM): $(COMUM_OBJ)
	$(AR) rcs $@ $^

$(BIN)/amr_robo: $(ROBO_OBJ) $(LIB_COMUM)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/amr_organizador: $(ORG_OBJ) $(LIB_COMUM)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/amr_gestor: $(GESTOR_OBJ) $(LIB_COMUM)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/amr_coletor: $(COLETOR_OBJ) $(LIB_COMUM)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BIN)/%: $(BUILD)/ferramentas/%.o $(LIB_COMUM)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

TESTE_OBJ := $(BUILD)/testes/teste_dominio.o $(BUILD)/src/organizador/despachante.o

$(BIN)/teste_dominio: $(TESTE_OBJ) $(LIB_COMUM)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

testes: $(BIN)/teste_dominio
	./$(BIN)/teste_dominio

tsan:
	$(MAKE) BUILD=build-tsan BIN=bin-tsan CXXFLAGS="-std=c++17 -O1 -g -fsanitize=thread -Wall -Wextra"

clean:
	rm -rf build build-tsan bin bin-tsan

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
