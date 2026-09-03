/** @type {import('next').NextConfig} */
const nextConfig = {
  // dashboard.template.html is read at runtime via fs, not imported —
  // must be explicitly told to the file tracer so Vercel bundles it
  // with the serverless function that serves "/".
  outputFileTracingIncludes: {
    "/": ["./lib/dashboard.template.html"],
  },
};

module.exports = nextConfig;
