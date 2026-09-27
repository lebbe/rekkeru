FROM node:24-alpine

WORKDIR /app
ENV NODE_ENV=production

# Copy package-files first, so Docker caches the npm-installastion
COPY package*.json ./
RUN npm ci --omit=dev

COPY . .

# Ikke kjør som root
USER node

EXPOSE 3000
CMD ["node", "index.ts"]
