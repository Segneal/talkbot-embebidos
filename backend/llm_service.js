/**
 * Claude via AWS Bedrock - LLM
 */

const { BedrockRuntimeClient, InvokeModelCommand } = require('@aws-sdk/client-bedrock-runtime');
const { searchWeb } = require('./search_service');
const { log } = require('./logger');

const region = process.env.AWS_REGION || 'us-east-1';
const modelId = process.env.BEDROCK_MODEL_ID || 'us.anthropic.claude-haiku-4-5-20251001-v1:0';

const BASE_RULES = `
REGLAS (cumplir siempre sin excepción):
1. BREVEDAD: Respondé en 1 oración corta, máximo 2 si es estrictamente necesario. Esto se reproduce como audio, si te extendés el usuario se aburre y la experiencia es mala.
2. SIN FORMATO: No uses listas, bullets, markdown, emojis ni numeración.
3. DIRECTO: No repitas la pregunta, no digas "buena pregunta", no des introducciones. Arrancá con la respuesta.
4. ARGENTINO: Hablás en español rioplatense educado. Usás "vos", "tenés", "podés". No uses jerga vulgar ni slang exagerado (nada de "flasheaste", "re piola", "posta", "chabón", etc). Soná natural pero con buena educación.
5. UBICACIÓN: Estás en Buenos Aires, Argentina. Asumí Buenos Aires para clima, noticias, eventos salvo que digan otra cosa.
6. DATOS ACTUALES: Si te piden clima, noticias, precios o datos en tiempo real, usá web_search. Nunca inventes datos.`;

const AGENT_PROMPTS = {
  lupe: `Sos Lupe, una asistente de voz argentina, amigable y cálida.
Hablás en rioplatense natural y educado. Sos simpática y vas al grano.
${BASE_RULES}`,
  pedro: `Sos Pedro, un asistente de voz argentino, formal y profesional.
Hablás en rioplatense con tono respetuoso y claro. Sos cortés, preciso y servicial.
${BASE_RULES}`,
  mia: `Sos Mia, una asistente de voz argentina, relajada y amigable.
Hablás en rioplatense natural, con buena onda pero sin ser vulgar. Sos divertida y directa.
${BASE_RULES}`,
};

const TOOLS = [{
  name: 'web_search',
  description: 'Busca información actual en internet. Usa esta herramienta cuando el usuario pregunte sobre clima, noticias, eventos, datos en tiempo real o cualquier cosa que requiera información actualizada.',
  input_schema: {
    type: 'object',
    properties: {
      query: { type: 'string', description: 'La consulta de búsqueda en internet' },
    },
    required: ['query'],
  },
}];

const TOOL_HANDLERS = {
  web_search: async (input) => searchWeb(input.query),
};

const client = new BedrockRuntimeClient({ region });
let conversationHistory = [];
let currentAgent = 'lupe';
const MAX_HISTORY = 10;

async function generateResponse(userText, agentName) {
  // Si cambió el agente, limpiar historial
  if (agentName && agentName !== currentAgent) {
    conversationHistory = [];
    currentAgent = agentName;
    log('LLM', `Agente cambiado a: ${agentName}`);
  }

  conversationHistory.push({ role: 'user', content: userText });

  // Limitar historial
  if (conversationHistory.length > MAX_HISTORY * 2) {
    conversationHistory = conversationHistory.slice(-MAX_HISTORY * 2);
  }

  try {
    const systemPrompt = AGENT_PROMPTS[currentAgent] || AGENT_PROMPTS.lupe;
    const assistantText = await invokeWithToolLoop(systemPrompt);

    if (assistantText) {
      conversationHistory.push({ role: 'assistant', content: assistantText });
    } else {
      conversationHistory.pop(); // sacar el user message si no hubo respuesta
    }

    log('LLM', `Respuesta: ${assistantText}`);
    return assistantText;
  } catch (err) {
    // Sacar el mensaje del usuario para no romper el historial
    conversationHistory.pop();
    throw err;
  }
}

async function invokeWithToolLoop(systemPrompt) {
  let messages = [...conversationHistory];

  // Loop para manejar tool_use (máximo 3 iteraciones por seguridad)
  for (let i = 0; i < 3; i++) {
    const body = JSON.stringify({
      anthropic_version: 'bedrock-2023-05-31',
      max_tokens: 160,
      system: systemPrompt,
      messages,
      tools: TOOLS,
    });

    const response = await client.send(new InvokeModelCommand({
      modelId,
      contentType: 'application/json',
      accept: 'application/json',
      body,
    }));

    const result = JSON.parse(new TextDecoder().decode(response.body));

    // Si no hay tool_use, extraer texto y retornar
    if (result.stop_reason !== 'tool_use') {
      const textBlock = result.content.find(b => b.type === 'text');
      return textBlock ? textBlock.text : '';
    }

    // Hay tool_use: ejecutar la herramienta
    const toolUseBlock = result.content.find(b => b.type === 'tool_use');
    if (!toolUseBlock) {
      const textBlock = result.content.find(b => b.type === 'text');
      return textBlock ? textBlock.text : '';
    }

    log('LLM', `Tool use: ${toolUseBlock.name}(${JSON.stringify(toolUseBlock.input)})`);

    // Ejecutar la herramienta
    const handler = TOOL_HANDLERS[toolUseBlock.name];
    let toolResult;
    if (handler) {
      toolResult = await handler(toolUseBlock.input);
    } else {
      toolResult = `Herramienta "${toolUseBlock.name}" no disponible`;
    }

    log('LLM', `Tool result: ${toolResult.slice(0, 200)}`);

    // Agregar la respuesta del asistente con tool_use y el tool_result
    messages.push({ role: 'assistant', content: result.content });
    messages.push({
      role: 'user',
      content: [{
        type: 'tool_result',
        tool_use_id: toolUseBlock.id,
        content: toolResult,
      }],
    });
  }

  // Si llegamos aquí, se agotaron las iteraciones
  return 'Lo siento, no pude completar la búsqueda.';
}

function clearHistory() {
  conversationHistory = [];
}

module.exports = { generateResponse, clearHistory };
