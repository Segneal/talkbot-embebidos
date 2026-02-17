/**
 * Tavily Web Search Service
 */

const { log } = require('./logger');
const TAVILY_API_KEY = process.env.TAVILY_API_KEY;

async function searchWeb(query) {
  if (!TAVILY_API_KEY) {
    return 'Error: TAVILY_API_KEY no configurada';
  }

  log('Search', `Buscando: "${query}"`);

  const response = await fetch('https://api.tavily.com/search', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      api_key: TAVILY_API_KEY,
      query,
      search_depth: 'basic',
      max_results: 3,
    }),
  });

  if (!response.ok) {
    const errorText = await response.text();
    log('Search', `Error ${response.status}: ${errorText}`);
    return `Error en búsqueda: ${response.status}`;
  }

  const data = await response.json();
  const results = (data.results || []).slice(0, 3);

  // Formatear resultados compactos para no inflar el contexto
  const summary = results
    .map(r => `- ${r.title}: ${r.content}`)
    .join('\n')
    .slice(0, 500);

  log('Search', `${results.length} resultados encontrados`);
  return summary || 'No se encontraron resultados.';
}

module.exports = { searchWeb };
