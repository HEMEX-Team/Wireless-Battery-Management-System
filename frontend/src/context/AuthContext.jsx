import { createContext, useContext, useState, useEffect } from "react";
import { apiFetch } from "../lib/api";

const AuthContext = createContext(null);

export function AuthProvider({ children }) {
  const [user, setUser] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const token = localStorage.getItem("wbms_token");
    if (!token) {
      setLoading(false);
      return;
    }
    apiFetch("/v1/auth/me")
      .then((data) => setUser(data))
      .catch(() => {
        localStorage.removeItem("wbms_token");
      })
      .finally(() => setLoading(false));
  }, []);

  async function applyToken(accessToken) {
    localStorage.setItem("wbms_token", accessToken);
    const me = await apiFetch("/v1/auth/me");
    setUser(me);
  }

  async function login(email, password) {
    const body = new URLSearchParams({ username: email, password });
    const data = await apiFetch("/v1/auth/login", { method: "POST", body });
    await applyToken(data.access_token);
  }

  async function register(firstName, lastName, email, password) {
    await apiFetch("/v1/auth/register", {
      method: "POST",
      body: JSON.stringify({
        first_name: firstName,
        last_name: lastName,
        email,
        password,
      }),
    });
    await login(email, password);
  }

  async function loginWithGoogle(credential) {
    const data = await apiFetch("/v1/auth/google", {
      method: "POST",
      body: JSON.stringify({ credential }),
    });
    await applyToken(data.access_token);
  }

  function logout() {
    localStorage.removeItem("wbms_token");
    setUser(null);
  }

  return (
    <AuthContext.Provider
      value={{
        user,
        loading,
        isAuthenticated: !!user,
        login,
        register,
        loginWithGoogle,
        logout,
      }}
    >
      {children}
    </AuthContext.Provider>
  );
}

// This hook is co-located with the provider by convention. The fast-refresh
// rule wants one component per file; exporting the hook here is intentional.
// eslint-disable-next-line react-refresh/only-export-components
export function useAuth() {
  const ctx = useContext(AuthContext);
  if (!ctx) throw new Error("useAuth must be used within AuthProvider");
  return ctx;
}
