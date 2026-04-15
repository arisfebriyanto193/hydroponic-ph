import type { Metadata } from 'next';
import './globals.css';

export const metadata: Metadata = {
  title: 'IoT Hydroponics Control',
  description: 'Hydroponics Control Dashboard',
};

export default function RootLayout({
  children,
}: {
  children: React.ReactNode;
}) {
  return (
    <html lang="id">
      <body>
        <div className="app-container">
          {children}
        </div>
      </body>
    </html>
  );
}
