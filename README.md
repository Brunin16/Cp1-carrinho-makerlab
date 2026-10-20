# CP2 — TinyML aplicado ao Carrinho-Robô

Evolução do carrinho do [CP1](../README.md): o sensor ultrassônico frontal passou a alimentar um modelo de Machine Learning (Edge Impulse) embarcado no ESP32, e a **classe inferida muda o comportamento do carrinho**.

**Fluxo:** Sensor HC-SR04 → coleta (Serial) → CSV → Edge Impulse → treino → inferência → biblioteca Arduino → ESP32 → ação no carrinho.

## 👥 Integrantes

| Nome | RM |
| ---- | -- |
| Bruno Caputo | 558303 |
| Lucas Rodrigues | 98344 |
| Vinicius Yamashita | 550908 |

## 📁 Estrutura desta pasta

| Pasta | Conteúdo |
| ----- | -------- |
| `dataset/` | 15 CSVs (`dataset/csv/`) e `LEIAME_dataset.txt` com a metodologia de coleta e rotulagem |
| `edge-impulse/prints/` | Evidências do processo no Edge Impulse (numeradas na ordem do trabalho) |
| `firmware/carrinho_cp2/` | Sketch do ESP32 com a inferência integrada ao carrinho |
| `firmware/coleta/` | Sketch usado para gravar o dataset (**[ADICIONAR]**) |
| `video/` | Vídeo da demonstração (**[ADICIONAR]**) |

---

## 1. Dataset e coleta de dados

- **Sensor:** HC-SR04 frontal do carrinho (o mesmo do CP1), lido pelo ESP32 a **10 Hz** (uma leitura a cada 100 ms).
- **Dado coletado:** distância frontal em cm (`dist_cm`), 1 eixo.
- **Como foi coletado:** o ESP32 imprimiu `timestamp,dist_cm` no Serial (115200) e o log bruto (~208 s) foi copiado do Monitor Serial. Foi uma gravação única, com várias situações: obstáculo parado perto, área livre e aproximações. Os trechos usados estão listados no `dataset/LEIAME_dataset.txt`.
- **Pré-processamento (igual no firmware):** sem eco ou ≥ 200 cm → 200 cm; leitura < 2 cm → repete a última válida; mediana das 3 últimas leituras. Implementado em `lerDistanciaTratada()` para que a inferência veja dados iguais aos do treino.

### Classes

| Classe | Definição (regra aplicada sobre janelas de 2 s = 20 amostras, passo de 0,5 s) |
| ------ | ------ |
| `obstaculo` | todas as leituras ≤ 22 cm e desvio padrão ≤ 3 cm (obstáculo parado perto) |
| `livre` | todas as leituras ≥ 40 cm, sem queda relevante (nada próximo / sem eco) |
| `aproximando` | queda ≥ 15 cm ao longo da janela, começando em ≥ 25 cm, gradual e sem subir no final |

### Volume

| Classe | Arquivos | Amostras | Duração | Janelas (2 s / passo 0,5 s) |
| ------ | -------: | -------: | ------: | --------------------------: |
| aproximando | 5 | 105 | 10,5 s | 6 |
| livre | 5 | 180 | 18,0 s | 21 |
| obstaculo | 5 | 320 | 32,0 s | 49 |
| **Total** | **15** | **605** | **60,5 s** | **76** |

Arquivos: `dataset/csv/<classe>.00N.csv`, formato `timestamp,dist_cm` (ms, começando em 0). O rótulo é o texto antes do primeiro ponto, como o Edge Impulse espera.

### Limitações

- Volume pequeno, principalmente de `aproximando` (5 arquivos).
- Os rótulos foram atribuídos **por regras de distância/tendência**, e não por anotação manual durante a gravação.
- `obstaculo` está concentrado entre 7 e 19 cm; faltam outras distâncias e ângulos.

---

## 2. Projeto no Edge Impulse

Projeto: `Brunin-project-1`. Evidências em [`edge-impulse/prints/`](edge-impulse/prints/).

1. **Importação:** upload dos 15 CSVs com *Infer label from filename* (`01_dataset_importado.png`): 1 min 1 s de dados, 3 classes.
2. **Divisão:** split automático do Studio, 12 arquivos em treino e 3 em teste (≈ 89 % / 11 %).
3. **Impulse:** Time series, janela de **2000 ms**, passo de **500 ms**, **10 Hz**, 1 eixo (`dist_cm`).
4. **Processamento e modelo — três iterações:**

| Iteração | Bloco | Resultado (validação) | Diagnóstico |
| -------- | ----- | --------------------- | ----------- |
| 1 | Flatten (7 estatísticas), parâmetros padrão | acurácia 53,3 %, loss 1,00 | underfitting: features sem escala e learning rate baixo (`05_…`) |
| 2 | Flatten, ajustes de escala e treino | acurácia 93,3 %, loss 2,74, **`aproximando` 0 %** | o Flatten descarta a ordem temporal e `aproximando` é definida por tendência; a acurácia alta escondia a classe perdida (`06_…`) |
| 3 | **Raw data** (20 valores em ordem) + rede densa | acurácia 93,3 %, loss 0,13, AUC 0,97 (`07_…`) | modelo bem calibrado; `aproximando` ainda cai em `livre` na validação |

   Parâmetros finais de treino: **[CONFIRMAR no Studio: Scale axes, ciclos, learning rate, camadas, auto-balance, data augmentation]**.

5. **Model testing (conjunto de teste, float32)** — `08_…` e `09_…`:

| Métrica | Valor |
| ------- | ----: |
| Acurácia | 75,00 % |
| Weighted precision / recall / F1 | 0,58 / 0,75 / 0,65 |
| F1 por classe | livre 1,00 · obstaculo 0,80 · aproximando 0,00 |

   Matriz de confusão: `livre` → 100 % correto; `obstaculo` → 100 % correto; `aproximando` → 100 % classificado como `obstaculo`.

   > O conjunto de teste tem apenas 3 arquivos (cerca de 1 janela por classe), então essas métricas **não têm peso estatístico**. O resultado confiável é qualitativo: o modelo separa `livre` de `obstaculo` e não aprendeu `aproximando`.

6. **Exportação:** Deployment → Arduino library, EON Compiler, modelo **[CONFIRMAR: int8 ou float32]** (`10_…`). Estimativa: ~1,4 KB de RAM e ~13–15 KB de flash.

### Análise

- `aproximando` não foi aprendida: poucos exemplos (6 janelas) e rótulos por regra. O erro é do lado seguro: a classe cai em `obstaculo`, e o carrinho reage como se houvesse obstáculo.
- O erro perigoso (`obstaculo` classificado como `livre`) **não ocorreu** no teste.
- Próximos passos: gravar mais exemplos de `aproximando` e `livre` com o firmware atualizado, em outras distâncias e ângulos, com rotulagem manual durante a gravação, e re-treinar.

---

## 3. Integração com o ESP32

Sketch: [`firmware/carrinho_cp2/carrinho_cp2.ino`](firmware/carrinho_cp2/carrinho_cp2.ino). Biblioteca: `Brunin-project-1_inferencing` (zip exportado do Edge Impulse; instalar via *Add .ZIP Library*).

- O **botão SELECT** do Dabble liga/desliga o modo TinyML. O modo manual e o piloto automático (Triângulo) continuam como no CP1.
- O ESP32 lê o HC-SR04 a 10 Hz, aplica o filtro (`lerDistanciaTratada()`), mantém uma janela deslizante de 20 amostras e executa `run_classifier()` a cada 500 ms.
- A classe vencedora (confiança ≥ 0,60) define a ação:

| Classe | Ação no carrinho |
| ------ | ---------------- |
| `livre` | segue em frente, face feliz animada |
| `aproximando` | metade da velocidade, face de surpreso, bipe curto |
| `obstaculo` | para os motores, face de susto, bipe |

- Se o loop ficar bloqueado (dança, sirene, melodia), a janela é descartada e reenchida (2 s), para não classificar dados velhos.
- **Freio de emergência** (`TINYML_FREIO_EMERGENCIA`): para se a distância ≤ 5 cm, independentemente do modelo. É uma rede de segurança por causa do atraso da janela de 2 s.
- No Monitor Serial aparecem as probabilidades das 3 classes e `ACAO -> <classe>` a cada inferência.

## 4. Evolução CP1 → CP2

| | CP1 | CP2 |
| - | --- | --- |
| Sensor | HC-SR04, limiar fixo (15 cm) no piloto automático | HC-SR04, leitura filtrada alimentando um modelo TinyML |
| Decisão | `if distância < limite` | classificação em 3 classes pela rede neural no ESP32 |
| Código | `src/` | `CP2/firmware/` (mantendo `src/` do CP1 intacto) |

O histórico de commits mostra a evolução: dataset → Edge Impulse → firmware → integração → documentação.

## 5. Demonstração

Vídeo: **[ADICIONAR link/arquivo em `video/`]**
