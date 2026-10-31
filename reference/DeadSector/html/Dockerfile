# Prepare verified dependencies and browser build first: npm ci && npm run build.
# The server only needs ws; copy the lockfile-installed package, with no network at image build time.
FROM node:24-alpine
WORKDIR /app
ENV NODE_ENV=production
COPY --chown=node:node package.json package-lock.json ./
COPY --chown=node:node node_modules/ws ./node_modules/ws
COPY --chown=node:node dist ./dist
COPY --chown=node:node server ./server
COPY --chown=node:node weapon-config.js economy.js waves.js ./
USER node
EXPOSE 3000
CMD ["node", "server/index.js"]
