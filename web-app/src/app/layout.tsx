import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'Hydroponics Dashboard',
  description: 'Advanced Hydroponics Control Center',
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="id">
      <body>
        <div className="app-layout">
          {children}
        </div>
      </body>
    </html>
  );
}
