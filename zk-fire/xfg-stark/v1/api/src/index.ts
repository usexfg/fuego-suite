// COLD Deposits API Server
// Express server with domain linking and Fuego daemon integration

import express, { Application, Request, Response, NextFunction } from 'express';
import cors from 'cors';
import helmet from 'helmet';
import morgan from 'morgan';
import dotenv from 'dotenv';
import claimRouter from './routes/claim';

// Load environment variables
dotenv.config();

const app: Application = express();
const PORT = process.env.PORT || 3000;

// Middleware
app.use(helmet()); // Security headers
app.use(cors({
  origin: process.env.ALLOWED_ORIGINS?.split(',') || ['http://localhost:3000', 'https://usexfg.org'],
  credentials: true
}));
app.use(morgan('combined')); // Request logging
app.use(express.json()); // Parse JSON bodies

// Rate limiting (simple in-memory version - use Redis in production)
const requestCounts = new Map<string, { count: number; resetTime: number }>();

function rateLimit(req: Request, res: Response, next: NextFunction) {
  const ip = req.ip || 'unknown';
  const now = Date.now();
  const windowMs = 60 * 1000; // 1 minute
  const maxRequests = 10; // 10 requests per minute

  let record = requestCounts.get(ip);

  if (!record || now > record.resetTime) {
    record = { count: 1, resetTime: now + windowMs };
    requestCounts.set(ip, record);
    return next();
  }

  if (record.count >= maxRequests) {
    return res.status(429).json({
      success: false,
      error: 'Too many requests. Please try again later.'
    });
  }

  record.count++;
  next();
}

app.use(rateLimit);

// Routes
app.use('/api/cold', claimRouter);

// Root endpoint
app.get('/', (req: Request, res: Response) => {
  res.json({
    name: 'COLD Deposits API',
    version: '1.0.0',
    status: 'running',
    endpoints: {
      claim: 'POST /api/cold/claim',
      health: 'GET /api/cold/health'
    }
  });
});

// 404 handler
app.use((req: Request, res: Response) => {
  res.status(404).json({
    success: false,
    error: 'Endpoint not found'
  });
});

// Error handler
app.use((err: Error, req: Request, res: Response, next: NextFunction) => {
  console.error('Unhandled error:', err);
  res.status(500).json({
    success: false,
    error: 'Internal server error'
  });
});

// Start server
app.listen(PORT, () => {
  console.log(`❄️  COLD Deposits API running on port ${PORT}`);
  console.log(`Environment: ${process.env.NODE_ENV || 'development'}`);
  console.log(`Allowed origins: ${process.env.ALLOWED_ORIGINS || 'localhost'}`);
});

// Graceful shutdown
process.on('SIGTERM', () => {
  console.log('SIGTERM received, shutting down gracefully...');
  process.exit(0);
});

process.on('SIGINT', () => {
  console.log('SIGINT received, shutting down gracefully...');
  process.exit(0);
});
