--
-- PostgreSQL database dump
--

\restrict d29JxAVhBl8EkyrIfUPWVl8tRumKEkHQMaIgRFaq4oPr0MZmnUpne4Xo9OgJMna

-- Dumped from database version 18.3
-- Dumped by pg_dump version 18.3

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET transaction_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: books; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.books (
    id integer NOT NULL,
    subgenre_id integer,
    title text NOT NULL,
    author text NOT NULL,
    publisher text,
    year integer DEFAULT 0 NOT NULL,
    format text,
    rating real DEFAULT 0 NOT NULL,
    price real DEFAULT 0 NOT NULL,
    age_rating text,
    isbn text,
    total_print_run bigint DEFAULT 0 NOT NULL,
    signed_to_print_date text,
    additional_print_dates text,
    cover_image_path text,
    license_image_path text,
    bibliographic_reference text,
    cover_url text,
    search_frequency real DEFAULT 1 NOT NULL,
    created_at timestamp without time zone DEFAULT now() NOT NULL,
    total_circulation bigint DEFAULT 0,
    print_sign_date text DEFAULT ''::text,
    additional_prints text DEFAULT '[]'::text,
    bibliographic_ref text DEFAULT ''::text,
    CONSTRAINT books_age_rating_check CHECK (((age_rating = ANY (ARRAY['0+'::text, '6+'::text, '12+'::text, '16+'::text, '18+'::text])) OR (age_rating = ''::text)))
);


ALTER TABLE public.books OWNER TO postgres;

--
-- Name: books_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.books_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.books_id_seq OWNER TO postgres;

--
-- Name: books_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.books_id_seq OWNED BY public.books.id;


--
-- Name: genres; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.genres (
    id integer NOT NULL,
    name text NOT NULL
);


ALTER TABLE public.genres OWNER TO postgres;

--
-- Name: genres_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.genres_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.genres_id_seq OWNER TO postgres;

--
-- Name: genres_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.genres_id_seq OWNED BY public.genres.id;


--
-- Name: subgenres; Type: TABLE; Schema: public; Owner: postgres
--

CREATE TABLE public.subgenres (
    id integer NOT NULL,
    genre_id integer NOT NULL,
    name text NOT NULL
);


ALTER TABLE public.subgenres OWNER TO postgres;

--
-- Name: subgenres_id_seq; Type: SEQUENCE; Schema: public; Owner: postgres
--

CREATE SEQUENCE public.subgenres_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.subgenres_id_seq OWNER TO postgres;

--
-- Name: subgenres_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: postgres
--

ALTER SEQUENCE public.subgenres_id_seq OWNED BY public.subgenres.id;


--
-- Name: books id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.books ALTER COLUMN id SET DEFAULT nextval('public.books_id_seq'::regclass);


--
-- Name: genres id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.genres ALTER COLUMN id SET DEFAULT nextval('public.genres_id_seq'::regclass);


--
-- Name: subgenres id; Type: DEFAULT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subgenres ALTER COLUMN id SET DEFAULT nextval('public.subgenres_id_seq'::regclass);


--
-- Data for Name: books; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY public.books (id, subgenre_id, title, author, publisher, year, format, rating, price, age_rating, isbn, total_print_run, signed_to_print_date, additional_print_dates, cover_image_path, license_image_path, bibliographic_reference, cover_url, search_frequency, created_at, total_circulation, print_sign_date, additional_prints, bibliographic_ref) FROM stdin;
\.


--
-- Data for Name: genres; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY public.genres (id, name) FROM stdin;
\.


--
-- Data for Name: subgenres; Type: TABLE DATA; Schema: public; Owner: postgres
--

COPY public.subgenres (id, genre_id, name) FROM stdin;
\.


--
-- Name: books_id_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('public.books_id_seq', 1, false);


--
-- Name: genres_id_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('public.genres_id_seq', 27, true);


--
-- Name: subgenres_id_seq; Type: SEQUENCE SET; Schema: public; Owner: postgres
--

SELECT pg_catalog.setval('public.subgenres_id_seq', 27, true);


--
-- Name: books books_isbn_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.books
    ADD CONSTRAINT books_isbn_key UNIQUE (isbn);


--
-- Name: books books_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.books
    ADD CONSTRAINT books_pkey PRIMARY KEY (id);


--
-- Name: genres genres_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.genres
    ADD CONSTRAINT genres_name_key UNIQUE (name);


--
-- Name: genres genres_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.genres
    ADD CONSTRAINT genres_pkey PRIMARY KEY (id);


--
-- Name: subgenres subgenres_genre_id_name_key; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subgenres
    ADD CONSTRAINT subgenres_genre_id_name_key UNIQUE (genre_id, name);


--
-- Name: subgenres subgenres_pkey; Type: CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subgenres
    ADD CONSTRAINT subgenres_pkey PRIMARY KEY (id);


--
-- Name: idx_books_author; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_books_author ON public.books USING btree (author);


--
-- Name: idx_books_isbn; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_books_isbn ON public.books USING btree (isbn);


--
-- Name: idx_books_subgenre_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_books_subgenre_id ON public.books USING btree (subgenre_id);


--
-- Name: idx_books_title; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_books_title ON public.books USING btree (title);


--
-- Name: idx_subgenres_genre_id; Type: INDEX; Schema: public; Owner: postgres
--

CREATE INDEX idx_subgenres_genre_id ON public.subgenres USING btree (genre_id);


--
-- Name: books books_subgenre_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.books
    ADD CONSTRAINT books_subgenre_id_fkey FOREIGN KEY (subgenre_id) REFERENCES public.subgenres(id) ON DELETE SET NULL;


--
-- Name: subgenres subgenres_genre_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: postgres
--

ALTER TABLE ONLY public.subgenres
    ADD CONSTRAINT subgenres_genre_id_fkey FOREIGN KEY (genre_id) REFERENCES public.genres(id) ON DELETE CASCADE;


--
-- PostgreSQL database dump complete
--

\unrestrict d29JxAVhBl8EkyrIfUPWVl8tRumKEkHQMaIgRFaq4oPr0MZmnUpne4Xo9OgJMna

